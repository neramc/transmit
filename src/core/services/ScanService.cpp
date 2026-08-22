#include "core/services/ScanService.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>

#include <filesystem>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/FileIo.h"

#ifndef Q_OS_WIN
#include <sys/stat.h>
#include <grp.h>
#include <pwd.h>
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

    if (const passwd* user = ::getpwuid(status.st_uid)) {
        posix.userName = user->pw_name;
    }
    if (const group* grp = ::getgrgid(status.st_gid)) {
        posix.groupName = grp->gr_name;
    }
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

ExcludeMatcher::ExcludeMatcher(const QStringList& patterns) { add(patterns); }

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

ScanService::ScanService(const platform::PlatformService& platformService)
    : platform_(platformService), tokens_(platformService.knownFolders()) {}

ScanResult ScanService::scan(const CaptureSelection& selection, CancelToken& cancelToken,
                             const ProgressCallback& progress) const {
    ScanResult result;

    ExcludeMatcher globalExcludes(selection.globalExcludePatterns);

    for (const CaptureRoot& root : selection.roots) {
        if (cancelToken.isCancelled()) {
            break;
        }
        if (!selection.includes(root.domain)) {
            continue;
        }
        scanRoot(root, selection, globalExcludes, result, cancelToken, progress);
    }

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

    const QString rootPath =
        root.relative.isEmpty()
            ? fromUtf8(*base)
            : fromUtf8(format::joinPath(*base, toUtf8(root.relative)));

    const QFileInfo rootInfo(rootPath);
    if (!rootInfo.exists()) {
        qCDebug(logCapture) << "skipping missing capture root" << rootPath;
        return;
    }

    ExcludeMatcher rootExcludes(root.excludePatterns);

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
            item.symlinkTarget = ec ? info.symLinkTarget()
                                    : fromUtf8(format::fromFsPath(rawTarget));
            ++result.symlinkCount;
        } else if (info.isDir()) {
            item.type = format::EntryType::Directory;
            ++result.directoryCount;
        } else {
            item.type = format::EntryType::File;
            item.size = static_cast<quint64>(std::max<qint64>(info.size(), 0));

            if (selection.maximumFileSize > 0 && item.size > selection.maximumFileSize) {
                ++result.skippedCount;
                result.notes.push_back(ContinuityNote{
                    ContinuityGrade::Manual, root.domain, absolute,
                    QObject::tr("Skipped because it is larger than the size limit you set (%1).")
                        .arg(formatBytes(selection.maximumFileSize))});
                return;
            }
            if (!info.isReadable()) {
                item.problem = QObject::tr("could not be read");
                result.notes.push_back(ContinuityNote{
                    ContinuityGrade::Manual, root.domain, absolute,
                    QObject::tr("This file could not be read, so it was not captured. It may "
                                "belong to another user or need administrator rights.")});
                ++result.skippedCount;
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
            progress(update);
        }
    };

    if (rootInfo.isDir() && !rootInfo.isSymLink()) {
        record(rootInfo);

        QDirIterator::IteratorFlags flags = root.recursive ? QDirIterator::Subdirectories
                                                           : QDirIterator::NoIteratorFlags;
        if (selection.followSymlinks) {
            flags |= QDirIterator::FollowSymlinks;
        }

        QDirIterator iterator(rootPath, QDir::AllEntries | QDir::Hidden | QDir::System |
                                            QDir::NoDotAndDotDot,
                              flags);
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
