#include "core/services/ImportService.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>

#include <optional>
#include <unordered_map>
#include <vector>

#include "core/recipe/AppInventoryPayload.h"
#include "core/recipe/InstallScriptWriter.h"
#include "core/recipe/RecipeCatalog.h"
#include "core/recipe/StateRelocator.h"
#include "core/rewrite/PathRewriter.h"
#include "core/rewrite/PathTranslator.h"
#include "core/secrets/SecretsDomain.h"
#include "core/services/RollbackWriter.h"
#include "core/settings/SettingsDomain.h"
#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/FileIo.h"
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

/// Writes one restored file into place.
///
/// QSaveFile, which this used to be, flushes to the operating system and
/// renames - so a crash is survivable but a power cut is not: ext4 and NTFS
/// are both free to commit the rename before the data, which leaves a file of
/// the right name and the wrong length. On a restore that is silent data loss
/// wearing the shape of success, so the bytes go to the device first.
///
/// The parent folder is deliberately not flushed here. The caller collects the
/// folders it touched and syncs each once, because a restore commonly writes
/// thousands of files into a handful of directories and one flush per file
/// would be almost all of the cost for none of the benefit.
bool writeFileContents(const QString& path, const format::ByteBuffer& content, QString& error,
                       bool durable) {
    const auto status = format::writeFileAtomically(
        format::toFsPath(path.toUtf8().toStdString()), format::ByteView(content),
        durable ? format::Durability::Data : format::Durability::Buffered);
    if (!status) {
        error = describeError(status.error());
        return false;
    }
    return true;
}

/// Produces "name~1.ext" for the keep-both policy, skipping names already used,
/// or nothing when ten thousand of them are taken.
///
/// It used to return the original path in that case, which under KeepBoth -
/// the default policy - meant the one outcome the user explicitly asked to
/// avoid: their existing file overwritten, reported as a success.
std::optional<QString> uniqueSibling(const QString& path) {
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
    return std::nullopt;
}

/// What has to exist before what.
///
/// Folders first, so nothing is written into a path that is not there yet and
/// every file lands inside a folder whose ownership and mode are already
/// decided. Symlinks last, so their targets exist by the time they are made.
///
/// This used to be the raw enum value, which is File, Directory, Symlink - the
/// exact reverse of the first half, and the opposite of what the comment next
/// to it claimed.
int restoreOrder(format::EntryType type) noexcept {
    switch (type) {
        case format::EntryType::Directory:
            return 0;
        case format::EntryType::File:
            return 1;
        case format::EntryType::Symlink:
            return 2;
    }
    return 3;
}

}  // namespace

ImportService::ImportService(platform::PlatformService& platformService)
    : platform_(platformService) {}

ArchiveSummary ImportService::inspect(const QString& archivePath, const QString& passphrase) const {
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
            map.setBase(token,
                        format::joinPath(toUtf8(root), std::string(format::tokenName(token))));
        }
        return map;
    }

    if (request.emulateOs != OsFamily::Unknown && request.emulateOs != platform_.environment().os) {
        // A dry run against another OS: model that OS's layout from this
        // machine's home directory so the report shows real translated paths.
        return format::PathTokenMap::defaultsFor(request.emulateOs,
                                                 toUtf8(platform_.environment().homeDirectory));
    }

    return platform_.knownFolders();
}

void ImportService::previewRewrites(format::ArchiveReader& reader, const format::Manifest& manifest,
                                    const ImportRequest& request,
                                    const format::PathTokenMap& targetFolders, OsFamily targetOs,
                                    ImportReport& report) const {
    const auto* inventory = manifest.findPayload(DomainId::AppInventory, "apps.v1");
    if (inventory == nullptr) {
        return;
    }

    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        return;
    }

    // A folder table rooted in the scratch directory, mirroring the shape the
    // real restore would produce.
    format::PathTokenMap scratchFolders(targetOs);
    for (const format::PathTokenId token : format::allTokens()) {
        if (token == format::PathTokenId::Absolute) {
            continue;
        }
        scratchFolders.setBase(
            token, format::joinPath(toUtf8(scratch.path()), std::string(format::tokenName(token))));
    }

    format::NameSanitizer sanitizer(format::SanitizeOptions::forTarget(targetOs));
    const QList<InventoryEntry> entries = decodeAppInventory(inventory->data);
    const StateRelocator relocator(entries, manifest.source.os, targetOs);
    int unpacked = 0;

    for (const format::ManifestEntry& entry : manifest.entries) {
        if (entry.domain != DomainId::AppState || entry.type != format::EntryType::File) {
            continue;
        }
        const format::TokenizedPath placed = relocator.relocate(entry.path);
        const format::TokenizedPath safePath{placed.token,
                                             sanitizer.sanitizeRelativePath(placed.relative)};
        const auto resolved = scratchFolders.resolve(safePath);
        if (!resolved) {
            continue;
        }

        auto content = reader.readEntry(entry);
        if (!content) {
            continue;
        }

        const QString target = fromUtf8(*resolved);
        QDir().mkpath(QFileInfo(target).absolutePath());

        QString error;
        if (writeFileContents(target, *content, error, false)) {
            ++unpacked;
        }
    }

    if (unpacked == 0) {
        return;
    }

    // The translator still points at where the files would really land, so the
    // reported values are the ones a real restore would write.
    PathTranslator translator(manifest.source, targetFolders, targetOs);
    translator.setRenames(report.renames);
    translator.setRelocator(&relocator);

    const PathRewriter rewriter(translator);
    RewritePlan plan;

    for (const InventoryEntry& entry : entries) {
        const AppRecipe recipe = entry.toRecipe();
        for (const RecipeStatePath& state : recipe.state) {
            const QString tokenised = state.forOs(targetOs).isEmpty()
                                          ? state.forOs(manifest.source.os)
                                          : state.forOs(targetOs);
            if (tokenised.isEmpty()) {
                continue;
            }
            const QString stateRoot = RecipeCatalog::resolveStatePath(tokenised, scratchFolders);
            if (!stateRoot.isEmpty()) {
                rewriter.planFor(recipe, stateRoot, plan);
            }
        }
    }

    // Report the real destinations rather than the scratch copies.
    for (ContinuityNote note : plan.toNotes()) {
        note.subject.replace(scratch.path(), QString());
        report.notes.push_back(note);
    }
    Q_UNUSED(request);
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
            update.phase = ProgressPhase::Verifying;
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
        report.notes.push_back(
            ContinuityNote{ContinuityGrade::Adapted, DomainId::Unknown,
                           QCoreApplication::translate("Import", "Operating system"),
                           QCoreApplication::translate(
                               "Import",
                               "This archive was captured on %1 and is being restored onto %2. "
                               "Locations and file names are translated to match.")
                               .arg(fromUtf8(format::osFamilyName(manifest.source.os)),
                                    fromUtf8(format::osFamilyName(targetOs)))});
    }

    // An application looks for its settings in a different place on each
    // operating system, so state captured on one is moved to where the program
    // will actually find it here.
    QList<InventoryEntry> inventoryEntries;
    if (const auto* payload = manifest.findPayload(DomainId::AppInventory, "apps.v1")) {
        inventoryEntries = decodeAppInventory(payload->data);
    }
    const StateRelocator relocator(inventoryEntries, manifest.source.os, targetOs);

    if (relocator.hasRelocations()) {
        QStringList moved;
        for (const StateRelocator::Rule& rule : relocator.rules()) {
            moved << QStringLiteral("%1 (%2 to %3)")
                         .arg(rule.displayName, fromUtf8(rule.from.toDisplayString()),
                              fromUtf8(rule.to.toDisplayString()));
        }
        report.notes.push_back(ContinuityNote{
            ContinuityGrade::Adapted, DomainId::AppState,
            QCoreApplication::translate("Import", "Moved to where this system keeps them"),
            QCoreApplication::translate(
                "Import",
                "These programs keep their settings somewhere else on this system, so their "
                "data was placed where they will look for it: %1")
                .arg(moved.join(QStringLiteral("; ")))});
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
    // Directories first, and then the files in the order they lie in the
    // archive rather than the order the manifest happens to list them.
    //
    // Every file in one solid block is written before the next block is
    // touched, so each block is decompressed once - and on a stick the reads
    // go forwards through the file instead of seeking back and forth across
    // it. Manifest order is capture order, which is close to this but not the
    // same, and "close" is the difference between one pass and thousands of
    // seeks on a multi-part archive.
    //
    // Where each block sits is worked out once, not inside the comparator: a
    // sort calls that n log n times and finding a block is a walk down the
    // block list.
    std::unordered_map<std::uint32_t, std::uint64_t> blockPositions;
    blockPositions.reserve(manifest.blocks.size());
    for (const format::BlockRecord& block : manifest.blocks) {
        blockPositions.emplace(block.blockId, block.streamOffset);
    }

    struct Placed {
        const format::ManifestEntry* entry = nullptr;
        int order = 0;
        std::uint64_t blockOffset = 0;
        std::uint64_t withinBlock = 0;
    };
    std::vector<Placed> byPosition;
    byPosition.reserve(static_cast<std::size_t>(ordered.size()));
    for (const format::ManifestEntry* entry : ordered) {
        Placed item;
        item.entry = entry;
        item.order = restoreOrder(entry->type);
        if (entry->type == format::EntryType::File) {
            const auto found = blockPositions.find(entry->location.blockId);
            item.blockOffset = found == blockPositions.end() ? 0 : found->second;
            item.withinBlock = entry->location.offset;
        }
        byPosition.push_back(item);
    }

    std::stable_sort(byPosition.begin(), byPosition.end(), [](const Placed& a, const Placed& b) {
        if (a.order != b.order) {
            return a.order < b.order;
        }
        if (a.blockOffset != b.blockOffset) {
            return a.blockOffset < b.blockOffset;
        }
        return a.withinBlock < b.withinBlock;
    });

    for (qsizetype i = 0; i < ordered.size(); ++i) {
        ordered[i] = byPosition[static_cast<std::size_t>(i)].entry;
    }

    quint64 totalBytes = 0;
    for (const format::ManifestEntry* entry : ordered) {
        totalBytes += entry->size;
    }

    // Paths the undo point does not cover, so the write loop must not
    // touch them.
    QSet<QString> unbackedUp;

    // Folders whose ownership and mode still have to be applied. It cannot be
    // done as they are created: a folder that arrives read-only would then
    // refuse every file about to be written into it.
    std::vector<std::pair<QString, const format::ManifestEntry*>> restoredDirectories;

    // Every folder a file landed in. Their entries are flushed once at the
    // end rather than once per file: a restore writes thousands of files into
    // a few dozen folders, so per-file directory flushes would cost most of
    // the run for none of the safety.
    QSet<QString> touchedDirectories;

    if (request.createRollback && !request.dryRun) {
        QStringList intended;
        intended.reserve(ordered.size());

        format::NameSanitizer probe(format::SanitizeOptions::forTarget(targetOs));
        for (const format::ManifestEntry* entry : ordered) {
            if (entry->type != format::EntryType::File) {
                continue;
            }
            const format::TokenizedPath placed =
                entry->domain == DomainId::AppState ? relocator.relocate(entry->path) : entry->path;
            const format::TokenizedPath safePath{placed.token,
                                                 probe.sanitizeRelativePath(placed.relative)};
            if (const auto resolved = tokens.resolve(safePath)) {
                intended << fromUtf8(*resolved);
            }
        }

        const QString directory = request.destinationOverride.isEmpty()
                                      ? platform_.environment().homeDirectory
                                      : request.destinationOverride;

        auto rollback = RollbackWriter::capture(intended, directory);
        if (rollback) {
            report.rollbackArchivePath = rollback->archivePath;

            // A file that could not be read in full is not in the undo
            // point, so restoring over it would replace something that
            // cannot be put back. Left alone instead, and said so.
            for (const QString& path : rollback->unbackedUp) {
                unbackedUp.insert(path);
                report.notes.push_back(ContinuityNote{
                    ContinuityGrade::Manual, DomainId::Unknown, path,
                    QCoreApplication::translate(
                        "Import",
                        "This file could not be read in full, so it could not be saved for "
                        "undo. It was left as it is rather than replaced.")});
            }

            if (!report.rollbackArchivePath.isEmpty()) {
                report.notes.push_back(ContinuityNote{
                    ContinuityGrade::Full, DomainId::Unknown,
                    QCoreApplication::translate("Import", "You can undo this"),
                    QCoreApplication::translate(
                        "Import",
                        "Everything this restore is about to replace was saved first. To put it "
                        "back, run: transmit-cli rollback \"%1\"")
                        .arg(report.rollbackArchivePath)});
            }
        } else {
            // Not being able to take an undo point is worth saying, but it is
            // not a reason to refuse a restore the user asked for.
            report.notes.push_back(
                ContinuityNote{ContinuityGrade::Manual, DomainId::Unknown,
                               QCoreApplication::translate("Import", "No undo point"),
                               QCoreApplication::translate(
                                   "Import", "This restore could not be made reversible: %1")
                                   .arg(describeError(rollback.error()))});
        }
    }

    QElapsedTimer throttle;
    throttle.start();

    for (const format::ManifestEntry* entryPtr : ordered) {
        if (cancelToken.isCancelled()) {
            return fail(QCoreApplication::translate("Import", "Cancelled."));
        }
        const format::ManifestEntry& entry = *entryPtr;

        const format::TokenizedPath placed =
            entry.domain == DomainId::AppState ? relocator.relocate(entry.path) : entry.path;

        // Sanitising the relative part only: the known-folder base is already
        // valid on this machine.
        const std::string safeRelative = sanitizer.sanitizeRelativePath(placed.relative);
        const format::TokenizedPath safePath{placed.token, safeRelative};

        const auto resolved = tokens.resolve(safePath);
        if (!resolved) {
            report.notes.push_back(ContinuityNote{ContinuityGrade::Manual, entry.domain,
                                                  fromUtf8(entry.path.toDisplayString()),
                                                  describeError(resolved.error())});
            ++report.filesFailed;
            continue;
        }

        QString targetPath = fromUtf8(*resolved);
        RestoredItem item;
        item.sourcePath = fromUtf8(entry.path.toDisplayString());
        item.targetPath = targetPath;

        if (safeRelative != placed.relative) {
            item.grade = ContinuityGrade::Adapted;
            item.note = QCoreApplication::translate("Import",
                                                    "Renamed so the name is valid on this system.");
        }

        // ------------------------------------------------ conflicts
        const bool exists = QFileInfo::exists(targetPath);
        // Nothing may overwrite a file the undo point could not save.
        if (exists && unbackedUp.contains(targetPath)) {
            item.skipped = true;
            item.grade = ContinuityGrade::Manual;
            item.note = QCoreApplication::translate(
                "Import", "Left alone: it could not be saved for undo first.");
            ++report.filesSkipped;
            report.items.push_back(item);
            continue;
        }

        if (exists && entry.type != format::EntryType::Directory) {
            switch (request.conflictPolicy) {
                case ConflictPolicy::Skip:
                    item.skipped = true;
                    item.note = QCoreApplication::translate("Import",
                                                            "Left alone: a file is already here.");
                    ++report.filesSkipped;
                    report.items.push_back(item);
                    continue;
                case ConflictPolicy::KeepBoth: {
                    const std::optional<QString> alongside = uniqueSibling(targetPath);
                    if (!alongside) {
                        report.notes.push_back(ContinuityNote{
                            ContinuityGrade::Manual, entry.domain, targetPath,
                            QCoreApplication::translate(
                                "Import",
                                "There are already ten thousand files saved alongside this "
                                "one, so it was left in the archive rather than overwriting "
                                "anything.")});
                        ++report.filesFailed;
                        continue;
                    }
                    targetPath = *alongside;
                    item.targetPath = targetPath;
                    item.grade = ContinuityGrade::Adapted;
                    item.note = QCoreApplication::translate(
                        "Import", "Saved alongside the file that was already here.");
                    break;
                }
                case ConflictPolicy::NewerWins: {
                    const QFileInfo existing(targetPath);
                    const qint64 existingNs =
                        existing.lastModified().toMSecsSinceEpoch() * 1000000LL;
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
                    report.notes.push_back(
                        ContinuityNote{ContinuityGrade::Manual, entry.domain, targetPath,
                                       QCoreApplication::translate(
                                           "Import", "This folder could not be created.")});
                    ++report.filesFailed;
                    continue;
                }
                // The mode is deliberately not applied yet. A folder restored
                // as r-x cannot be written into, and every file inside it is
                // still to come; the second pass at the end of the run puts it
                // right once the folder is finished with.
                restoredDirectories.push_back({targetPath, &entry});
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
                    report.notes.push_back(
                        ContinuityNote{ContinuityGrade::Manual, entry.domain, targetPath,
                                       QCoreApplication::translate(
                                           "Import", "This symbolic link could not be created.")});
                    ++report.filesFailed;
                    continue;
                }
                break;
            }
            case format::EntryType::File: {
                const QString parentDirectory = QFileInfo(targetPath).absolutePath();
                QDir().mkpath(parentDirectory);
                touchedDirectories.insert(parentDirectory);

                auto content = reader->readEntry(entry);
                if (!content) {
                    report.notes.push_back(ContinuityNote{
                        ContinuityGrade::Manual, entry.domain, item.sourcePath,
                        QCoreApplication::translate("Import", "Could not be read back: %1")
                            .arg(describeError(content.error()))});
                    ++report.filesFailed;
                    continue;
                }

                QString error;
                if (!writeFileContents(targetPath, *content, error,
                                       request.durableWrites && !request.dryRun)) {
                    report.notes.push_back(ContinuityNote{
                        ContinuityGrade::Manual, entry.domain, targetPath,
                        QCoreApplication::translate("Import", "Could not be written: %1")
                            .arg(error)});
                    ++report.filesFailed;
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
            update.stage = request.dryRun ? QCoreApplication::translate(
                                                "Import", "Working out what would happen")
                                          : QCoreApplication::translate("Import", "Restoring");
            update.phase = ProgressPhase::Transferring;
            progress(update);
        }
    }

    for (const format::RenameRecord& rename : sanitizer.renames()) {
        report.renames.push_back({fromUtf8(rename.original), fromUtf8(rename.applied)});
    }

    // ------------------------------------------- repoint restored settings
    // Restoring an application's settings verbatim leaves it pointing at
    // directories from the old machine. The archive carries the recipe that
    // says which files hold those paths, so they can be corrected here.
    if (request.domains.isEmpty() ||
        request.domains.contains(static_cast<int>(DomainId::AppState))) {
        const auto* inventory = manifest.findPayload(DomainId::AppInventory, "apps.v1");

        if (inventory != nullptr && request.dryRun) {
            previewRewrites(*reader, manifest, request, tokens, targetOs, report);
        } else if (inventory != nullptr) {
            PathTranslator translator(manifest.source, tokens, targetOs);
            translator.setRenames(report.renames);
            translator.setRelocator(&relocator);

            const PathRewriter rewriter(translator);
            RewritePlan plan;

            for (const InventoryEntry& entry : inventoryEntries) {
                const AppRecipe recipe = entry.toRecipe();
                for (const RecipeStatePath& state : recipe.state) {
                    // Where that state landed here, which is what the rewrite
                    // rules are relative to.
                    const QString tokenised = state.forOs(targetOs).isEmpty()
                                                  ? state.forOs(manifest.source.os)
                                                  : state.forOs(targetOs);
                    if (tokenised.isEmpty()) {
                        continue;
                    }
                    const QString stateRoot = RecipeCatalog::resolveStatePath(tokenised, tokens);
                    if (!stateRoot.isEmpty()) {
                        rewriter.planFor(recipe, stateRoot, plan);
                    }
                }
            }

            if (!plan.isEmpty()) {
                report.notes += plan.toNotes();
                report.rewrittenFiles = plan.files();

                QStringList errors;
                const int changed = plan.apply(&errors);
                qCInfo(logRestore) << "repointed paths inside" << changed << "files";

                for (const QString& error : errors) {
                    report.notes.push_back(ContinuityNote{
                        ContinuityGrade::Manual, DomainId::AppState,
                        QCoreApplication::translate("Import", "Settings file"), error});
                }
                if (changed > 0) {
                    report.notes.push_back(ContinuityNote{
                        ContinuityGrade::Adapted, DomainId::AppState,
                        QCoreApplication::translate("Import", "Original settings kept"),
                        QCoreApplication::translate(
                            "Import",
                            "The version of each changed file as it arrived is kept next to it "
                            "with a .transmit-backup suffix. Undoing the restore removes them; "
                            "so does telling Transmit you are keeping it.")});
                }
            }
        }
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

    // ---------------------------------------------------- saved passwords
    if (request.domains.isEmpty() ||
        request.domains.contains(static_cast<int>(DomainId::Secrets))) {
        if (const auto* payload = manifest.findPayload(DomainId::Secrets, "secrets.v1")) {
            const bool emulating = request.emulateOs != OsFamily::Unknown &&
                                   request.emulateOs != platform_.environment().os;
            const QString scriptDirectory = request.destinationOverride.isEmpty()
                                                ? platform_.environment().homeDirectory
                                                : request.destinationOverride;

            report.notes += SecretsDomain(platform_).restore(payload->data, scriptDirectory,
                                                             request.dryRun || emulating);
        }
    }

    // ------------------------------------------------ desktop preferences
    if (request.domains.isEmpty() ||
        request.domains.contains(static_cast<int>(DomainId::SystemSettings))) {
        if (const auto* payload = manifest.findPayload(DomainId::SystemSettings, "settings.v1")) {
            const QList<CapturedSetting> settings = SettingsDomain::decode(payload->data);

            // Emulating another operating system is a translation exercise on
            // paper; actually changing this machine's preferences would be a
            // surprise nobody asked for.
            const bool emulating = request.emulateOs != OsFamily::Unknown &&
                                   request.emulateOs != platform_.environment().os;

            const QString scriptDirectory = request.destinationOverride.isEmpty()
                                                ? platform_.environment().homeDirectory
                                                : request.destinationOverride;

            report.notes += SettingsDomain(platform_).restore(settings, scriptDirectory,
                                                              request.dryRun || emulating);
        }
    }

    // ------------------------------------------- reinstalling programs
    // The settings are in place; what is missing is the programs themselves,
    // which cannot cross an operating system boundary as binaries.
    if (!inventoryEntries.isEmpty() &&
        (request.domains.isEmpty() ||
         request.domains.contains(static_cast<int>(DomainId::AppInventory)))) {
        const InstallScriptWriter writer(platform_);
        const InstallPlan installPlan = writer.plan(inventoryEntries);

        report.programsToInstall = static_cast<int>(installPlan.installable.size());
        report.programsNeedingManualInstall = static_cast<int>(installPlan.manual.size());

        if (!request.dryRun && !installPlan.isEmpty()) {
            const QString directory = request.destinationOverride.isEmpty()
                                          ? platform_.environment().homeDirectory
                                          : request.destinationOverride;
            report.installScriptPath = writer.write(installPlan, directory);
        }

        if (!installPlan.installable.isEmpty()) {
            report.notes.push_back(ContinuityNote{
                ContinuityGrade::Manual, DomainId::AppInventory,
                QCoreApplication::translate("Import", "Programs to reinstall"),
                report.installScriptPath.isEmpty()
                    ? QCoreApplication::translate(
                          "Import", "%n program(s) from your old computer can be installed here.",
                          nullptr, static_cast<int>(installPlan.installable.size()))
                    : QCoreApplication::translate(
                          "Import",
                          "%n program(s) can be installed here. Transmit wrote a script to \"%1\" "
                          "but has not run it - read it first, then run it yourself.",
                          nullptr, static_cast<int>(installPlan.installable.size()))
                          .arg(report.installScriptPath)});
        }

        if (!installPlan.manual.isEmpty()) {
            report.notes.push_back(ContinuityNote{
                ContinuityGrade::Manual, DomainId::AppInventory,
                QCoreApplication::translate("Import", "Programs to install by hand"),
                QCoreApplication::translate(
                    "Import",
                    "No package manager here offers these, so install them yourself. Their "
                    "settings are already restored: %1")
                    .arg(installPlan.manual.join(QStringLiteral(", ")))});
        }
    }

    // Now that nothing more will be written into them, the folders can have
    // the mode they arrived with. Deepest first, so a folder is never made
    // unreadable before the pass has been inside it.
    if (!request.dryRun) {
        std::stable_sort(
            restoredDirectories.begin(), restoredDirectories.end(),
            [](const auto& a, const auto& b) { return a.first.count(u'/') > b.first.count(u'/'); });
        for (const auto& [directoryPath, entry] : restoredDirectories) {
            applyMetadata(directoryPath, *entry, targetOs);
        }
    }

    // The files are on the device; their names are not yet. One flush per
    // folder settles that, and it is the last thing the restore does so a
    // report that says a file is there means the machine agrees.
    if (request.durableWrites && !request.dryRun) {
        for (const QString& directory : touchedDirectories) {
            const auto status =
                format::syncDirectory(format::toFsPath(directory.toUtf8().toStdString()));
            if (!status) {
                qCWarning(logRestore)
                    << "could not flush" << directory << describeError(status.error());
            }
        }
    }

    // Not unconditional. A restore where every file failed used to report
    // success and exit 0, which made the most common real failure -
    // somewhere unwritable, a stick pulled half way - invisible to every
    // caller.
    report.succeeded = report.filesFailed == 0;
    if (!report.succeeded) {
        report.errorMessage =
            QCoreApplication::translate("Import", "%n file(s) could not be restored.", nullptr,
                                        static_cast<int>(report.filesFailed));
    }
    report.elapsedMilliseconds = timer.elapsed();

    qCInfo(logRestore) << "restored" << report.filesRestored << "items,"
                       << formatBytes(report.bytesWritten) << "in" << report.elapsedMilliseconds
                       << "ms," << report.filesFailed << "failed," << report.filesSkipped
                       << "skipped";
    return report;
}

}  // namespace transmit::core
