#include "core/services/ImportService.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <optional>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/NameSanitizer.h"

#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

namespace transmit::core {
namespace {

constexpr qint64 kProgressIntervalMs = 100;

/// Applies the recorded modification time and, where the target supports them,
/// the POSIX permission bits. Ownership is deliberately not restored: the
/// numeric ids from the source machine rarely mean the same thing here, and
/// changing owner needs privileges the app should not ask for.
void applyMetadata(const QString& path, const format::ManifestEntry& entry, OsFamily target) {
#ifndef Q_OS_WIN
    if (entry.posix.isSet() && target != OsFamily::Windows) {
        ::chmod(QFile::encodeName(path).constData(), static_cast<mode_t>(entry.posix.mode));
    }
#else
    Q_UNUSED(path);
    Q_UNUSED(entry);
    Q_UNUSED(target);
#endif
}

bool writeFileContents(const QString& path, const format::ByteBuffer& content, QString& error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    if (!content.empty()) {
        const qint64 written = file.write(reinterpret_cast<const char*>(content.data()),
                                          static_cast<qint64>(content.size()));
        if (written != static_cast<qint64>(content.size())) {
            error = file.errorString();
            return false;
        }
    }
    if (!file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

/// Produces "name~1.ext" for the keep-both policy, skipping names already used.
QString uniqueSibling(const QString& path) {
    const QFileInfo info(path);
    const QString stem = info.completeBaseName();
    const QString suffix = info.suffix();
    const QString directory = info.absolutePath();

    for (int index = 1; index < 10000; ++index) {
        QString candidate = QStringLiteral("%1/%2~%3").arg(directory, stem).arg(index);
        if (!suffix.isEmpty()) {
            candidate += u'.' + suffix;
        }
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return path;
}

}  // namespace

ImportService::ImportService(platform::PlatformService& platformService)
    : platform_(platformService) {}

ArchiveSummary ImportService::inspect(const QString& archivePath,
                                      const QString& passphrase) const {
    ArchiveSummary summary;

    auto readerResult = format::ArchiveReader::open(format::toFsPath(toUtf8(archivePath)));
    if (!readerResult) {
        summary.errorMessage = describeError(readerResult.error());
        return summary;
    }
    auto reader = std::move(readerResult).value();

    summary.valid = true;
    summary.encrypted = reader->isEncrypted();
    summary.partCount = static_cast<int>(reader->partCount());
    summary.capturedAt = QDateTime::fromSecsSinceEpoch(reader->createdUnix());

    if (summary.encrypted) {
        if (passphrase.isEmpty()) {
            return summary;  // locked, but the header details above are valid
        }
        if (const auto status = reader->unlock(toUtf8(passphrase)); !status) {
            summary.errorMessage = describeError(status.error());
            return summary;
        }
    }
    summary.unlocked = true;

    const auto manifest = reader->manifest();
    if (!manifest) {
        summary.errorMessage = describeError(manifest.error());
        summary.valid = false;
        return summary;
    }

    const format::Manifest& data = **manifest;
    summary.archiveId = fromUtf8(data.archiveId);
    summary.label = fromUtf8(data.label);
    summary.sourceOs = data.source.os;
    summary.sourceOsName = fromUtf8(data.source.osName);
    summary.sourceHost = fromUtf8(data.source.hostName);
    summary.sourceUser = fromUtf8(data.source.userName);
    summary.writtenByVersion = fromUtf8(data.source.appVersion);
    if (data.source.capturedUnix > 0) {
        summary.capturedAt = QDateTime::fromSecsSinceEpoch(data.source.capturedUnix);
    }

    for (const format::ManifestEntry& entry : data.entries) {
        if (entry.type == format::EntryType::File) {
            ++summary.fileCount;
            summary.rawBytes += entry.size;
            summary.filesPerDomain[static_cast<int>(entry.domain)] += 1;
            summary.bytesPerDomain[static_cast<int>(entry.domain)] += entry.size;
        }
    }
    return summary;
}

format::PathTokenMap ImportService::targetTokens(const ImportRequest& request) const {
    const OsFamily target =
        request.emulateOs == OsFamily::Unknown ? platform_.environment().os : request.emulateOs;

    // Restoring into a chosen folder, or emulating another OS, both mean the
    // real known folders are not the destination. A synthetic table rooted at
    // the destination keeps every path inside it.
    if (!request.destinationOverride.isEmpty()) {
        const QString root = QDir::cleanPath(request.destinationOverride);
        format::PathTokenMap map(target);
        for (const format::PathTokenId token : format::allTokens()) {
            if (token == format::PathTokenId::Absolute) {
                continue;
            }
            map.setBase(token, format::joinPath(toUtf8(root),
                                                std::string(format::tokenName(token))));
        }
        return map;
    }

    if (request.emulateOs != OsFamily::Unknown &&
        request.emulateOs != platform_.environment().os) {
        // A dry run against another OS: model that OS's layout from this
        // machine's home directory so the report shows real translated paths.
        return format::PathTokenMap::defaultsFor(
            request.emulateOs, toUtf8(platform_.environment().homeDirectory));
    }

    return platform_.knownFolders();
}

ImportReport ImportService::run(const ImportRequest& request, CancelToken& cancelToken,
                                const ProgressCallback& progress) {
    ImportReport report;
    QElapsedTimer timer;
    timer.start();

    const auto fail = [&report, &timer](const QString& message) {
        report.succeeded = false;
        report.errorMessage = message;
        report.elapsedMilliseconds = timer.elapsed();
        qCWarning(logRestore) << "restore failed:" << message;
        return report;
    };

    auto readerResult = format::ArchiveReader::open(format::toFsPath(toUtf8(request.archivePath)));
    if (!readerResult) {
        return fail(describeError(readerResult.error()));
    }
    auto reader = std::move(readerResult).value();

    if (reader->isEncrypted()) {
        if (request.passphrase.isEmpty()) {
            return fail(QCoreApplication::translate(
                "Import", "This archive is encrypted. Enter its passphrase to continue."));
        }
        if (const auto status = reader->unlock(toUtf8(request.passphrase)); !status) {
            return fail(describeError(status.error()));
        }
    }

    if (request.verifyFirst) {
        if (progress) {
            ProgressUpdate update;
            update.stage = QCoreApplication::translate("Import", "Checking the archive");
            progress(update);
        }
        const auto status = reader->verifyAllBlocks(
            [&cancelToken](std::size_t, std::size_t) { return !cancelToken.isCancelled(); });
        if (!status) {
            return fail(describeError(status.error()));
        }
    }

    const auto manifestResult = reader->manifest();
    if (!manifestResult) {
        return fail(describeError(manifestResult.error()));
    }
    const format::Manifest& manifest = **manifestResult;

    const OsFamily targetOs =
        request.emulateOs == OsFamily::Unknown ? platform_.environment().os : request.emulateOs;
    const format::PathTokenMap tokens = targetTokens(request);

    format::NameSanitizer sanitizer(format::SanitizeOptions::forTarget(targetOs));

    // The source machine's folder layout, used to recognise absolute paths that
    // were captured pointing into it.
    std::optional<format::PathTokenMap> sourceTokens;
    if (!manifest.source.tokenBases.empty()) {
        format::PathTokenMap map(manifest.source.os);
        for (const auto& [token, base] : manifest.source.tokenBases) {
            map.setBase(token, base);
        }
        sourceTokens = std::move(map);
    }

    if (manifest.source.os != targetOs && manifest.source.os != OsFamily::Unknown) {
        report.notes.push_back(ContinuityNote{
            ContinuityGrade::Adapted, DomainId::Unknown,
            QCoreApplication::translate("Import", "Operating system"),
            QCoreApplication::translate(
                "Import", "This archive was captured on %1 and is being restored onto %2. "
                          "Locations and file names are translated to match.")
                .arg(fromUtf8(format::osFamilyName(manifest.source.os)),
                     fromUtf8(format::osFamilyName(targetOs)))});
    }

    // Directories first, so a file never arrives before its parent exists.
    QList<const format::ManifestEntry*> ordered;
    ordered.reserve(static_cast<qsizetype>(manifest.entries.size()));
    for (const format::ManifestEntry& entry : manifest.entries) {
        if (!request.domains.isEmpty() &&
            !request.domains.contains(static_cast<int>(entry.domain))) {
            continue;
        }
        ordered.push_back(&entry);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const format::ManifestEntry* a, const format::ManifestEntry* b) {
                         return static_cast<int>(a->type) < static_cast<int>(b->type);
                     });

    quint64 totalBytes = 0;
    for (const format::ManifestEntry* entry : ordered) {
        totalBytes += entry->size;
    }

    QElapsedTimer throttle;
    throttle.start();

    for (const format::ManifestEntry* entryPtr : ordered) {
        if (cancelToken.isCancelled()) {
            return fail(QCoreApplication::translate("Import", "Cancelled."));
        }
        const format::ManifestEntry& entry = *entryPtr;

        // Sanitising the relative part only: the known-folder base is already
        // valid on this machine.
        const std::string safeRelative = sanitizer.sanitizeRelativePath(entry.path.relative);
        const format::TokenizedPath safePath{entry.path.token, safeRelative};

        const auto resolved = tokens.resolve(safePath);
        if (!resolved) {
            report.notes.push_back(ContinuityNote{
                ContinuityGrade::Manual, entry.domain, fromUtf8(entry.path.toDisplayString()),
                describeError(resolved.error())});
            ++report.filesSkipped;
            continue;
        }

        QString targetPath = fromUtf8(*resolved);
        RestoredItem item;
        item.sourcePath = fromUtf8(entry.path.toDisplayString());
        item.targetPath = targetPath;

        if (safeRelative != entry.path.relative) {
            item.grade = ContinuityGrade::Adapted;
            item.note = QCoreApplication::translate(
                "Import", "Renamed so the name is valid on this system.");
        }

        // ------------------------------------------------ conflicts
        const bool exists = QFileInfo::exists(targetPath);
        if (exists && entry.type != format::EntryType::Directory) {
            switch (request.conflictPolicy) {
                case ConflictPolicy::Skip:
                    item.skipped = true;
                    item.note = QCoreApplication::translate("Import",
                                                            "Left alone: a file is already here.");
                    ++report.filesSkipped;
                    report.items.push_back(item);
                    continue;
                case ConflictPolicy::KeepBoth:
                    targetPath = uniqueSibling(targetPath);
                    item.targetPath = targetPath;
                    item.grade = ContinuityGrade::Adapted;
                    item.note = QCoreApplication::translate(
                        "Import", "Saved alongside the file that was already here.");
                    break;
                case ConflictPolicy::NewerWins: {
                    const QFileInfo existing(targetPath);
                    const qint64 existingNs = existing.lastModified().toMSecsSinceEpoch() * 1000000LL;
                    if (existingNs >= entry.modifiedUnixNs) {
                        item.skipped = true;
                        item.note = QCoreApplication::translate(
                            "Import", "Left alone: the file already here is newer.");
                        ++report.filesSkipped;
                        report.items.push_back(item);
                        continue;
                    }
                    break;
                }
                case ConflictPolicy::Overwrite:
                    break;
            }
        }

        if (request.dryRun) {
            report.items.push_back(item);
            ++report.filesRestored;
            continue;
        }

        // --------------------------------------------------- write
        switch (entry.type) {
            case format::EntryType::Directory: {
                if (!QDir().mkpath(targetPath)) {
                    report.notes.push_back(ContinuityNote{
                        ContinuityGrade::Manual, entry.domain, targetPath,
                        QCoreApplication::translate("Import", "This folder could not be created.")});
                    ++report.filesSkipped;
                    continue;
                }
                applyMetadata(targetPath, entry, targetOs);
                break;
            }
            case format::EntryType::Symlink: {
                QDir().mkpath(QFileInfo(targetPath).absolutePath());

                // A relative target travels as-is. An absolute one that pointed
                // inside a known folder on the source machine is re-pointed at
                // this machine's equivalent, otherwise it would dangle.
                QString linkTarget = fromUtf8(entry.symlinkTarget);
                if (!linkTarget.isEmpty() && sourceTokens.has_value()) {
                    const format::TokenizedPath tokenised =
                        sourceTokens->tokenize(entry.symlinkTarget);
                    if (!tokenised.isAbsoluteFallback()) {
                        if (const auto remapped = tokens.resolve(tokenised)) {
                            linkTarget = fromUtf8(*remapped);
                            item.grade = ContinuityGrade::Adapted;
                            item.note = QCoreApplication::translate(
                                "Import", "The link now points at the matching folder here.");
                        }
                    }
                }
                if (targetOs == OsFamily::Windows) {
                    // Creating a symbolic link on Windows needs either developer
                    // mode or administrator rights, so this is reported rather
                    // than attempted and silently failed.
                    report.notes.push_back(ContinuityNote{
                        ContinuityGrade::Manual, entry.domain, targetPath,
                        QCoreApplication::translate(
                            "Import",
                            "This is a symbolic link to \"%1\". Windows only allows creating one "
                            "with developer mode or administrator rights, so it was not restored.")
                            .arg(fromUtf8(entry.symlinkTarget))});
                    item.grade = ContinuityGrade::Manual;
                    ++report.filesSkipped;
                    report.items.push_back(item);
                    continue;
                }
                if (exists) {
                    QFile::remove(targetPath);
                }
                if (!QFile::link(linkTarget, targetPath)) {
                    report.notes.push_back(ContinuityNote{
                        ContinuityGrade::Manual, entry.domain, targetPath,
                        QCoreApplication::translate("Import",
                                                    "This symbolic link could not be created.")});
                    ++report.filesSkipped;
                    continue;
                }
                break;
            }
            case format::EntryType::File: {
                QDir().mkpath(QFileInfo(targetPath).absolutePath());

                auto content = reader->readEntry(entry);
                if (!content) {
                    report.notes.push_back(ContinuityNote{
                        ContinuityGrade::Manual, entry.domain, item.sourcePath,
                        QCoreApplication::translate("Import", "Could not be read back: %1")
                            .arg(describeError(content.error()))});
                    ++report.filesSkipped;
                    continue;
                }

                QString error;
                if (!writeFileContents(targetPath, *content, error)) {
                    report.notes.push_back(ContinuityNote{
                        ContinuityGrade::Manual, entry.domain, targetPath,
                        QCoreApplication::translate("Import", "Could not be written: %1")
                            .arg(error)});
                    ++report.filesSkipped;
                    continue;
                }
                applyMetadata(targetPath, entry, targetOs);
                report.bytesWritten += entry.size;
                break;
            }
        }

        ++report.filesRestored;
        report.items.push_back(item);

        if (progress && throttle.elapsed() >= kProgressIntervalMs) {
            throttle.restart();
            ProgressUpdate update;
            update.filesDone = report.filesRestored;
            update.filesTotal = static_cast<quint64>(ordered.size());
            update.bytesDone = report.bytesWritten;
            update.bytesTotal = totalBytes;
            update.currentItem = targetPath;
            update.stage = request.dryRun
                               ? QCoreApplication::translate("Import", "Working out what would happen")
                               : QCoreApplication::translate("Import", "Restoring");
            progress(update);
        }
    }

    for (const format::RenameRecord& rename : sanitizer.renames()) {
        report.renames.push_back({fromUtf8(rename.original), fromUtf8(rename.applied)});
    }
    if (!report.renames.isEmpty()) {
        report.notes.push_back(ContinuityNote{
            ContinuityGrade::Adapted, DomainId::Unknown,
            QCoreApplication::translate("Import", "Renamed files"),
            QCoreApplication::translate(
                "Import",
                "%n item(s) were renamed because their names are not valid on this system, or "
                "because two of them differ only by capitalisation.",
                nullptr, static_cast<int>(report.renames.size()))});
    }

    report.succeeded = true;
    report.elapsedMilliseconds = timer.elapsed();
    qCInfo(logRestore) << "restored" << report.filesRestored << "items,"
                       << formatBytes(report.bytesWritten) << "in" << report.elapsedMilliseconds
                       << "ms";
    return report;
}

}  // namespace transmit::core
