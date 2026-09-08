#include "core/services/ScanService.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/FileIo.h"
#include "format/FileTraits.h"

#ifndef Q_OS_WIN
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

namespace transmit::core {
namespace {

/// Progress is reported at most this often, so a scan of a million files does
/// not spend its time queuing signals to the UI thread.
constexpr qint64 kProgressIntervalMs = 100;

qint64 toUnixNs(const QDateTime& time) {
    if (!time.isValid()) {
        return 0;
    }
    return time.toMSecsSinceEpoch() * 1000000LL;
}

#ifndef Q_OS_WIN
/// The account name for a uid, and the group name for a gid.
///
/// getpwuid and getgrgid return a pointer into storage shared by the whole
/// process, so two scans running at once - or a scan and anything else that
/// asks - can read each other's answer, and a file ends up recorded as owned
/// by somebody else. The _r forms write into a buffer the caller owns.
///
/// A name that cannot be resolved is left empty rather than guessed at: the
/// numeric uid is recorded either way, and a restore onto another machine maps
/// by name only when it recognises one.
///
/// The answer is remembered per thread, because a home directory has one owner
/// and a scan asks about every file in it. The lookup is not cheap: it
/// allocates a sixteen-kilobyte buffer, and on a machine whose accounts come
/// from LDAP or SSSD it is a network round trip - two per file, for an answer
/// that never differs.
///
/// Per thread rather than shared, so there is no lock on the path every file
/// takes, and kept for the life of the thread: a uid does not change its name
/// while a capture is running, and if one somehow did, the number is recorded
/// alongside it and is what a restore falls back to.
template<typename Id, typename Record, typename Lookup>
std::string resolveName(Id id, Lookup lookup, char* Record::*field) {
    thread_local std::unordered_map<Id, std::string> known;
    if (const auto remembered = known.find(id); remembered != known.end()) {
        return remembered->second;
    }

    thread_local std::vector<char> buffer = [] {
        const long suggested = ::sysconf(_SC_GETPW_R_SIZE_MAX);
        return std::vector<char>(static_cast<std::size_t>(suggested > 0 ? suggested : 16384));
    }();

    Record record{};
    Record* found = nullptr;

    // A name that cannot be resolved is remembered as empty too: a uid with no
    // account behind it is exactly the case that would otherwise pay for the
    // full lookup on every single file.
    std::string name;
    if (lookup(id, &record, buffer.data(), buffer.size(), &found) == 0 && found != nullptr) {
        name = found->*field;
    }
    known.emplace(id, name);
    return name;
}
#endif

/// Reads the ownership and permission bits the manifest records. They are kept
/// even when capturing on Windows so a Linux-to-Linux move keeps its modes.
void fillPosixMetadata(const QFileInfo& info, ScannedItem& item) {
    format::PosixMetadata& posix = item.posix;
#ifndef Q_OS_WIN
    struct stat status {};
    const QByteArray nativePath = QFile::encodeName(info.absoluteFilePath());
    if (::lstat(nativePath.constData(), &status) != 0) {
        return;
    }
    posix.mode = static_cast<quint32>(status.st_mode & 07777);
    posix.uid = static_cast<quint32>(status.st_uid);
    posix.gid = static_cast<quint32>(status.st_gid);

    // The same stat already says how many names this file has, so asking costs
    // nothing here. Only regular files: a directory's link count is its
    // subdirectories and means something else entirely.
    if (S_ISREG(status.st_mode) && status.st_nlink > 1) {
        item.sharedVolume = static_cast<quint64>(status.st_dev);
        item.sharedFile = static_cast<quint64>(status.st_ino);
    }

    posix.userName =
        resolveName<::uid_t, struct ::passwd>(status.st_uid, ::getpwuid_r, &::passwd::pw_name);
    posix.groupName =
        resolveName<::gid_t, struct ::group>(status.st_gid, ::getgrgid_r, &::group::gr_name);
#else
    // Windows has no POSIX mode; the readable/writable pair is recorded so a
    // restore onto Linux produces something sensible rather than 0000.
    posix.mode = info.isWritable() ? 0644u : 0444u;
    if (info.isDir()) {
        posix.mode = info.isWritable() ? 0755u : 0555u;
    }
#endif
}

/// Reads the Windows attribute word: hidden, read-only, system, and the bits
/// that say the contents are somewhere else.
///
/// The manifest has had a place for this since the format was written and
/// nothing ever filled it in, so every capture made on Windows arrived with a
/// hidden file no longer hidden and a read-only one writable.
void fillWindowsMetadata(const QFileInfo& info, ScannedItem& item) {
    format::WindowsMetadata& windows = item.windows;
#ifdef Q_OS_WIN
    // The string has to outlive the pointer into it. Written as one expression
    // this was a temporary QString destroyed at the end of the statement, with
    // `native` left pointing into freed memory for every call after the first.
    const QString absolute = info.absoluteFilePath();
    const auto* native = reinterpret_cast<const wchar_t*>(absolute.utf16());
    const DWORD attributes = ::GetFileAttributesW(native);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    windows.attributes = static_cast<quint32>(attributes);

    // How many names the file has, which the attribute word does not say.
    // NTFS keeps it, and only a handle can be asked for it - so this costs an
    // open per file, next to nothing beside reading the file's contents, and
    // it is the only way to find out that two entries are one file. Files
    // whose contents are elsewhere are left alone: opening one fetches it.
    if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        format::storedInTheCloud(static_cast<std::uint32_t>(attributes))) {
        return;
    }
    const HANDLE handle = ::CreateFileW(
        native, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    BY_HANDLE_FILE_INFORMATION about{};
    if (::GetFileInformationByHandle(handle, &about) != 0 && about.nNumberOfLinks > 1) {
        item.sharedVolume = static_cast<quint64>(about.dwVolumeSerialNumber);
        item.sharedFile = (static_cast<quint64>(about.nFileIndexHigh) << 32) |
                          static_cast<quint64>(about.nFileIndexLow);
    }
    ::CloseHandle(handle);
#else
    Q_UNUSED(info);
    Q_UNUSED(windows);
    Q_UNUSED(item);
#endif
}

/// Reads the extended attributes worth carrying.
///
/// Every system Transmit runs on keeps a third thing beside a file's contents
/// and its permissions: named tags. Linux calls them extended attributes,
/// macOS the same, Windows keeps the equivalent in alternate data streams. The
/// two that share an interface are read here; what is worth carrying is
/// decided in one place, in the format layer, because the answer has to be the
/// same on the machine that writes an archive and the one that reads it.
void fillExtendedAttributes(const QFileInfo& info,
                            std::vector<format::ExtendedAttribute>& attributes) {
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    const QByteArray path = QFile::encodeName(info.absoluteFilePath());

#if defined(Q_OS_MACOS)
    const auto list = [&path](char* buffer, size_t size) {
        return ::listxattr(path.constData(), buffer, size, XATTR_NOFOLLOW);
    };
    const auto get = [&path](const char* name, void* buffer, size_t size) {
        return ::getxattr(path.constData(), name, buffer, size, 0, XATTR_NOFOLLOW);
    };
#else
    const auto list = [&path](char* buffer, size_t size) {
        return ::llistxattr(path.constData(), buffer, size);
    };
    const auto get = [&path](const char* name, void* buffer, size_t size) {
        return ::lgetxattr(path.constData(), name, buffer, size);
    };
#endif

    const ssize_t needed = list(nullptr, 0);
    if (needed <= 0) {
        return;  // none, or a filesystem that does not keep them
    }

    std::vector<char> names(static_cast<size_t>(needed));
    const ssize_t written = list(names.data(), names.size());
    if (written <= 0) {
        return;
    }

    // A NUL-separated list, which is why this walks rather than splits: an
    // attribute name may contain anything but a NUL.
    size_t at = 0;
    while (at < static_cast<size_t>(written)) {
        const std::string name(names.data() + at);
        at += name.size() + 1;
        if (name.empty() || !format::extendedAttributeTravels(name)) {
            continue;
        }

        const ssize_t size = get(name.c_str(), nullptr, 0);
        if (size < 0 || static_cast<size_t>(size) > format::kLargestExtendedAttribute) {
            continue;
        }

        std::string value(static_cast<size_t>(size), '\0');
        const ssize_t read = get(name.c_str(), value.data(), value.size());
        if (read < 0) {
            continue;  // it went away between the two calls
        }
        value.resize(static_cast<size_t>(read));
        attributes.push_back(format::ExtendedAttribute{name, std::move(value)});
    }
#else
    Q_UNUSED(info);
    Q_UNUSED(attributes);
#endif
}

/// Converts a wildcard pattern into a regular expression. "**" crosses
/// directory boundaries, "*" does not, which matches what users expect from
/// .gitignore-style patterns.
QRegularExpression compilePattern(const QString& pattern) {
    QString expression;
    expression.reserve(pattern.size() * 2);
    expression += QStringLiteral("\\A");

    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern[i];
        if (c == u'*') {
            if (i + 1 < pattern.size() && pattern[i + 1] == u'*') {
                expression += QStringLiteral(".*");
                ++i;
                // "**/" should also match zero directories, so "**/x" matches "x".
                if (i + 1 < pattern.size() && pattern[i + 1] == u'/') {
                    expression += QStringLiteral("(?:/|\\A)?");
                    ++i;
                }
            } else {
                expression += QStringLiteral("[^/]*");
            }
        } else if (c == u'?') {
            expression += QStringLiteral("[^/]");
        } else {
            expression += QRegularExpression::escape(QString(c));
        }
    }
    expression += QStringLiteral("\\z");

    QRegularExpression compiled(expression, QRegularExpression::CaseInsensitiveOption);
    compiled.optimize();
    return compiled;
}

}  // namespace

ExcludeMatcher::ExcludeMatcher(const QStringList& patterns) {
    add(patterns);
}

void ExcludeMatcher::add(const QStringList& patterns) {
    for (const QString& pattern : patterns) {
        if (!pattern.isEmpty()) {
            patterns_.push_back(compilePattern(pattern));
        }
    }
}

bool ExcludeMatcher::matches(const QString& relativePath) const {
    for (const QRegularExpression& pattern : patterns_) {
        if (pattern.match(relativePath).hasMatch()) {
            return true;
        }
    }
    return false;
}

bool storedOnlyInTheCloud(const QString& fileName, const format::WindowsMetadata& windows,
                          OsFamily host) {
    if (format::storedInTheCloud(windows.attributes)) {
        return true;
    }
    if (host != OsFamily::MacOs) {
        return false;
    }
    const QByteArray name = fileName.toUtf8();
    return format::isEvictedICloudFile(
        std::string_view(name.constData(), static_cast<size_t>(name.size())));
}

std::optional<SkipReason> ScopeRule::reject(quint64 size, const QFileInfo& info) const {
    if (maximumFileSize > 0 && size > maximumFileSize) {
        return SkipReason::TooLarge;
    }
    if (minimumFileSize > 0 && size < minimumFileSize) {
        return SkipReason::TooSmall;
    }
    if (!includeExtensions.isEmpty() || !excludeExtensions.isEmpty()) {
        const QString extension = info.suffix().toLower();
        if (!includeExtensions.isEmpty() && !includeExtensions.contains(extension)) {
            return SkipReason::WrongExtension;
        }
        if (excludeExtensions.contains(extension)) {
            return SkipReason::WrongExtension;
        }
    }
    if (modifiedSince.isValid() || modifiedBefore.isValid()) {
        const QDateTime modified = info.lastModified();
        if (modifiedSince.isValid() && modified < modifiedSince) {
            return SkipReason::TooOld;
        }
        if (modifiedBefore.isValid() && modified > modifiedBefore) {
            return SkipReason::TooNew;
        }
    }
    if (!includeHidden && info.isHidden()) {
        return SkipReason::Hidden;
    }
    return std::nullopt;
}

namespace {

/// Beyond this many, the individual notes stop earning their place: nobody
/// reads four thousand lines saying the same thing, and the counts by reason
/// say it better in one.
constexpr quint64 kMaxSkipNotes = 50;

}  // namespace

ScanService::ScanService(const platform::PlatformService& platformService)
    : tokens_(platformService.knownFolders()), host_(platformService.environment().os) {}

ScanResult ScanService::scan(const CaptureSelection& selection, CancelToken& cancelToken,
                             const ProgressCallback& progress) const {
    ScanResult result;

    ExcludeMatcher globalExcludes(selection.scope.excludePatterns);

    // Most specific first. Roots overlap by design - a recipe names Firefox's
    // own directory, and a profile may also sweep the whole configuration tree
    // it sits inside - and the order they are visited in decides which one
    // keeps a shared file, and therefore which application the report credits
    // it to. Insertion order gave that to whichever happened to be listed
    // first, so a broad sweep near the top of the list took every file in it
    // and the report credited them to nobody.
    QList<CaptureRoot> ordered;
    for (const CaptureRoot& root : selection.roots) {
        if (selection.includes(root.domain)) {
            ordered.push_back(root);
        }
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const CaptureRoot& a, const CaptureRoot& b) {
                         return a.specificity() > b.specificity();
                     });

    for (const CaptureRoot& root : ordered) {
        if (cancelToken.isCancelled()) {
            break;
        }
        scanRoot(root, selection, globalExcludes, result, cancelToken, progress);
    }

    QHash<QString, qsizetype> firstAt;
    QList<ScannedItem> unique;
    unique.reserve(result.items.size());

    for (ScannedItem& item : result.items) {
        const auto seen = firstAt.constFind(item.absolutePath);
        if (seen != firstAt.constEnd()) {
            // The duplicate is dropped, but not its application id. A broad
            // root reaching a file first used to mean the file was stored with
            // no owner even though a recipe also covered it; now the id is
            // promoted onto the copy that was kept.
            ScannedItem& kept = unique[seen.value()];
            if (kept.appId.isEmpty() && !item.appId.isEmpty()) {
                kept.appId = item.appId;
            }

            if (item.type == format::EntryType::File) {
                result.totalBytes -= item.size;
                --result.fileCount;
            } else if (item.type == format::EntryType::Directory) {
                --result.directoryCount;
            } else {
                --result.symlinkCount;
            }
            continue;
        }
        firstAt.insert(item.absolutePath, unique.size());
        unique.push_back(std::move(item));
    }
    result.items = std::move(unique);

    qCInfo(logCapture) << "scan found" << result.fileCount << "files," << result.directoryCount
                       << "directories," << result.symlinkCount << "links, totalling"
                       << formatBytes(result.totalBytes);
    return result;
}

void ScanService::scanRoot(const CaptureRoot& root, const CaptureSelection& selection,
                           const ExcludeMatcher& globalExcludes, ScanResult& result,
                           CancelToken& cancelToken, const ProgressCallback& progress) const {
    const auto base = tokens_.base(root.token);
    if (!base.has_value()) {
        result.notes.push_back(ContinuityNote{
            ContinuityGrade::Manual, root.domain,
            QStringLiteral("{%1}").arg(fromUtf8(format::tokenName(root.token))),
            QObject::tr("This machine has no location for that folder, so nothing was captured "
                        "from it.")});
        return;
    }

    const QString rootPath = root.relative.isEmpty()
                                 ? fromUtf8(*base)
                                 : fromUtf8(format::joinPath(*base, toUtf8(root.relative)));

    const QFileInfo rootInfo(rootPath);
    if (!rootInfo.exists()) {
        qCDebug(logCapture) << "skipping missing capture root" << rootPath;
        return;
    }

    ExcludeMatcher rootExcludes(root.excludePatterns);

    // A root may narrow the selection but not widen it, so the two are merged
    // rather than one replacing the other: somebody who set a size limit for
    // the whole capture does not expect one application to ignore it.
    const ScopeRule scope = selection.scope.narrowedBy(root.scope);

    QElapsedTimer throttle;
    throttle.start();

    // Directories are recorded too, so empty ones and their permissions survive.
    const auto record = [&](const QFileInfo& info) {
        const QString absolute = info.absoluteFilePath();
        const QString relativeToRoot = QDir(rootPath).relativeFilePath(absolute);

        if (!relativeToRoot.isEmpty() && relativeToRoot != QLatin1String(".") &&
            (globalExcludes.matches(relativeToRoot) || rootExcludes.matches(relativeToRoot))) {
            ++result.skippedCount;
            return;
        }

        ScannedItem item;
        item.absolutePath = absolute;
        item.tokenPath = tokens_.tokenize(toUtf8(absolute));
        item.domain = root.domain;
        item.appId = root.appId;
        item.modifiedUnixNs = toUnixNs(info.lastModified());
        item.createdUnixNs = toUnixNs(info.birthTime());
        fillPosixMetadata(info, item);
        fillWindowsMetadata(info, item);
        fillExtendedAttributes(info, item.extendedAttributes);

        if (info.isSymLink()) {
            item.type = format::EntryType::Symlink;
            // QFileInfo::symLinkTarget resolves to an absolute path, which
            // would turn a relative link into one pointing at the old machine's
            // layout. The raw target is what has to travel.
            std::error_code ec;
            const auto rawTarget =
                std::filesystem::read_symlink(format::toFsPath(toUtf8(absolute)), ec);
            item.symlinkTarget =
                ec ? info.symLinkTarget() : fromUtf8(format::fromFsPath(rawTarget));
            ++result.symlinkCount;
        } else if (info.isDir()) {
            item.type = format::EntryType::Directory;
            ++result.directoryCount;

            // Listing a folder needs read, entering it needs execute, and
            // QDirIterator does neither loudly: it walks straight past a
            // folder it cannot open, so an entire subtree can be missing from
            // the capture with nothing in the report to say so. The folder
            // itself is still recorded - it existed - but the gap is named.
            if (!info.isReadable() || !info.isExecutable()) {
                item.problem = QObject::tr("could not be looked inside");
                result.unreadableDirectories.push_back(absolute);
                result.notes.push_back(ContinuityNote{
                    ContinuityGrade::Manual, root.domain, absolute,
                    QObject::tr("This folder could not be opened, so nothing inside it was "
                                "captured. It may belong to another user or need administrator "
                                "rights.")});
            }
        } else {
            item.type = format::EntryType::File;
            item.size = static_cast<quint64>(std::max<qint64>(info.size(), 0));

            // The root's own rule when it has one, otherwise the selection's.
            // Counted by reason rather than only totalled: "412 excluded" tells
            // nobody whether to change anything, and "380 over the size limit"
            // tells them exactly what to change.
            if (const auto reason = scope.reject(item.size, info)) {
                ++result.skippedCount;
                result.skippedByReason[static_cast<int>(*reason)]++;
                if (result.skippedCount <= kMaxSkipNotes) {
                    result.notes.push_back(
                        ContinuityNote{ContinuityGrade::Manual, root.domain, absolute,
                                       QObject::tr("Left out: %1.").arg(skipReasonName(*reason))});
                }
                return;
            }
            // Before anything that would open the file. The size is right -
            // both systems keep it locally - so the count and the total tell
            // the person exactly what turning this on would cost.
            if (!scope.fetchCloudFiles &&
                storedOnlyInTheCloud(info.fileName(), item.windows, host_)) {
                ++result.skippedCount;
                result.skippedByReason[static_cast<int>(SkipReason::StoredInTheCloud)]++;
                result.cloudOnlyBytes += item.size;
                if (result.skippedCount <= kMaxSkipNotes) {
                    result.notes.push_back(ContinuityNote{
                        ContinuityGrade::Manual, root.domain, absolute,
                        QObject::tr("This file is kept online rather than on this machine, so it "
                                    "was listed but not read. Turn on \"download files kept "
                                    "online\" to fetch it.")});
                }
                return;
            }

            if (!info.isReadable()) {
                item.problem = QObject::tr("could not be read");
                result.notes.push_back(ContinuityNote{
                    ContinuityGrade::Manual, root.domain, absolute,
                    QObject::tr("This file could not be read, so it was not captured. It may "
                                "belong to another user or need administrator rights.")});
                ++result.skippedCount;
                result.skippedByReason[static_cast<int>(SkipReason::Unreadable)]++;
                return;
            }

            result.totalBytes += item.size;
            ++result.fileCount;
        }

        result.items.push_back(std::move(item));

        if (progress && throttle.elapsed() >= kProgressIntervalMs) {
            throttle.restart();
            ProgressUpdate update;
            update.filesDone = result.fileCount;
            update.bytesTotal = result.totalBytes;
            update.currentItem = absolute;
            update.stage = QObject::tr("Looking through your files");
            update.phase = ProgressPhase::Scanning;
            progress(update);
        }
    };

    if (rootInfo.isDir() && !rootInfo.isSymLink()) {
        record(rootInfo);

        QDirIterator::IteratorFlags flags =
            root.recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
        if (scope.followSymlinks) {
            flags |= QDirIterator::FollowSymlinks;
        }

        QDirIterator iterator(
            rootPath, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, flags);
        while (iterator.hasNext()) {
            if (cancelToken.isCancelled()) {
                return;
            }
            iterator.next();
            record(iterator.fileInfo());
        }
    } else {
        record(rootInfo);
    }
}

}  // namespace transmit::core
