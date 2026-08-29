#include "core/services/ScanService.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/FileIo.h"

#ifndef Q_OS_WIN
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
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
void fillPosixMetadata(const QFileInfo& info, format::PosixMetadata& posix) {
#ifndef Q_OS_WIN
    struct stat status {};
    const QByteArray nativePath = QFile::encodeName(info.absoluteFilePath());
    if (::lstat(nativePath.constData(), &status) != 0) {
        return;
    }
    posix.mode = static_cast<quint32>(status.st_mode & 07777);
    posix.uid = static_cast<quint32>(status.st_uid);
    posix.gid = static_cast<quint32>(status.st_gid);

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

/// The stricter of two rules, field by field. A root can only ever narrow what
/// the selection asked for.
ScopeRule narrowest(const ScopeRule& selection, const ScopeRule& root) {
    ScopeRule merged = selection;

    const auto tighterMaximum = [](quint64 a, quint64 b) {
        if (a == 0)
            return b;
        if (b == 0)
            return a;
        return std::min(a, b);
    };
    merged.maximumFileSize = tighterMaximum(selection.maximumFileSize, root.maximumFileSize);
    merged.minimumFileSize = std::max(selection.minimumFileSize, root.minimumFileSize);

    if (!root.includeExtensions.isEmpty()) {
        merged.includeExtensions = selection.includeExtensions.isEmpty()
                                       ? root.includeExtensions
                                       : selection.includeExtensions & root.includeExtensions;
    }
    merged.excludeExtensions |= root.excludeExtensions;

    if (root.modifiedSince.isValid() &&
        (!merged.modifiedSince.isValid() || root.modifiedSince > merged.modifiedSince)) {
        merged.modifiedSince = root.modifiedSince;
    }
    if (root.modifiedBefore.isValid() &&
        (!merged.modifiedBefore.isValid() || root.modifiedBefore < merged.modifiedBefore)) {
        merged.modifiedBefore = root.modifiedBefore;
    }

    merged.followSymlinks = selection.followSymlinks && root.followSymlinks;
    merged.includeHidden = selection.includeHidden && root.includeHidden;
    return merged;
}

}  // namespace

ScanService::ScanService(const platform::PlatformService& platformService)
    : tokens_(platformService.knownFolders()) {}

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
    const ScopeRule scope = narrowest(selection.scope, root.scope);

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
        fillPosixMetadata(info, item.posix);

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
