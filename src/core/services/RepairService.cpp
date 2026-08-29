#include "core/services/RepairService.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

#include <vector>

#include "core/services/ConsistentCopy.h"
#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/BlockPacker.h"
#include "format/Container.h"
#include "format/hash/ContentHash.h"

namespace transmit::core {
namespace {

RepairReport fail(RepairReport report, const QString& message) {
    report.succeeded = false;
    report.errorMessage = message;
    return report;
}

/// Where a file the archive names lived on the machine that captured it.
///
/// Resolved through the folder table the archive recorded rather than this
/// machine's current one: the point is to find the file that was captured,
/// and a person who has since moved their Documents folder has not moved the
/// file the archive is describing.
QString sourcePathOf(const format::ManifestEntry& entry, const format::SourceEnvironment& source) {
    const auto base = source.tokenBases.find(entry.path.token);
    if (base == source.tokenBases.end() || base->second.empty()) {
        return {};
    }
    return fromUtf8(format::joinPath(base->second, entry.path.relative));
}

}  // namespace

QString repairObstacleName(RepairObstacle obstacle) {
    switch (obstacle) {
        case RepairObstacle::SourceMissing:
            return QCoreApplication::translate("Repair", "no longer on this machine");
        case RepairObstacle::SourceChanged:
            return QCoreApplication::translate("Repair", "has changed since the capture");
        case RepairObstacle::SourceUnreadable:
            return QCoreApplication::translate("Repair", "could not be read");
    }
    return {};
}

RepairService::RepairService(const platform::PlatformService& platform) : platform_(platform) {}

RepairReport RepairService::run(const RepairRequest& request, CancelToken& cancelToken,
                                const ProgressCallback& progress) const {
    RepairReport report;

    // Which files need recovering. Given a list, that list; otherwise the
    // archive is read back off the drive and whatever fails is the answer.
    QStringList wanted = request.paths;
    if (wanted.isEmpty()) {
        VerifyRequest verification;
        verification.archivePath = request.archivePath;
        verification.passphrase = request.passphrase;
        verification.deep = true;

        const VerifyService verifier(platform_);
        const VerifyReport verified = verifier.run(verification, cancelToken, progress);
        for (const VerifyFileResult& failure : verified.failures) {
            // A file an existing repair already supplies is in that list
            // because the archive is damaged, not because the file is lost.
            // Recovering it again would recover something that is already
            // there and report it as work done.
            if (failure.fromRepair) {
                continue;
            }
            wanted.push_back(failure.path);
        }
        if (wanted.isEmpty()) {
            if (verified.filesChecked == 0) {
                // Nothing was examined at all - a damaged footer, a missing
                // part, the wrong passphrase. There is no list of files to
                // recover because nothing got far enough to make one, and
                // reporting that as "nothing to repair" would tell somebody
                // their archive is fine.
                //
                // Decided on what was checked rather than on whether the
                // verification passed: an archive that has already been
                // repaired never passes again - its parts no longer match the
                // checksums written beside them, and never will, because the
                // damaged original is deliberately left alone.
                return fail(
                    std::move(report),
                    verified.errorMessage.isEmpty()
                        ? QCoreApplication::translate("Repair", "This archive could not be read.")
                        : verified.errorMessage);
            }
            // Nothing to do, which is a success rather than an error: somebody
            // running `repair` on a sound archive should be told it is sound.
            report.succeeded = true;
            return report;
        }
    }
    report.filesNeedingRepair = static_cast<quint64>(wanted.size());

    const std::filesystem::path archivePath = format::toFsPath(toUtf8(request.archivePath));
    auto opening = format::ArchiveReader::open(archivePath);
    if (!opening) {
        return fail(std::move(report), describeError(opening.error()));
    }
    auto reader = std::move(opening).value();

    if (reader->isEncrypted()) {
        if (request.passphrase.isEmpty()) {
            return fail(std::move(report),
                        QCoreApplication::translate(
                            "Repair", "This archive is encrypted and no passphrase was given."));
        }
        if (const auto unlocked = reader->unlock(toUtf8(request.passphrase)); !unlocked) {
            return fail(std::move(report), describeError(unlocked.error()));
        }
    }

    const auto manifest = reader->manifest();
    if (!manifest) {
        return fail(std::move(report), describeError(manifest.error()));
    }

    // The entries being recovered, in the archive's own order.
    const QSet<QString> asked(wanted.constBegin(), wanted.constEnd());
    std::vector<const format::ManifestEntry*> entries;
    for (const format::ManifestEntry& entry : (*manifest)->entries) {
        if (entry.hasContent() && asked.contains(fromUtf8(entry.path.toDisplayString()))) {
            entries.push_back(&entry);
        }
    }
    if (entries.empty()) {
        return fail(std::move(report),
                    QCoreApplication::translate(
                        "Repair", "This archive holds none of the files named for repair."));
    }

    // Read every file first, and only write the repair archive if something
    // was actually recovered - so a repair that can recover nothing leaves no
    // file behind claiming otherwise.
    struct Recovered {
        const format::ManifestEntry* entry = nullptr;
        QByteArray content;
    };
    std::vector<Recovered> recovered;

    ProgressUpdate update;
    update.phase = ProgressPhase::Transferring;
    update.filesTotal = static_cast<quint64>(entries.size());
    update.stage = QCoreApplication::translate("Repair", "Reading the originals again");

    for (const format::ManifestEntry* entry : entries) {
        if (cancelToken.isCancelled()) {
            return fail(std::move(report), QCoreApplication::translate("Repair", "Cancelled."));
        }

        RepairFailure failure;
        failure.path = fromUtf8(entry->path.toDisplayString());
        failure.sourcePath = sourcePathOf(*entry, (*manifest)->source);

        update.currentItem = failure.path;
        update.filesDone = static_cast<quint64>(recovered.size());
        if (progress) {
            progress(update);
        }

        if (failure.sourcePath.isEmpty() || !QFileInfo::exists(failure.sourcePath)) {
            failure.obstacle = RepairObstacle::SourceMissing;
            failure.detail =
                QCoreApplication::translate(
                    "Repair", "It was captured from %1, and there is nothing there now.")
                    .arg(failure.sourcePath.isEmpty() ? QCoreApplication::translate("Repair",
                                                                                    "a folder this "
                                                                                    "machine does "
                                                                                    "not have")
                                                      : failure.sourcePath);
            report.failures.push_back(failure);
            continue;
        }

        // Checked before reading, and reported as a change rather than as a
        // read error: a file that is now a different length is still perfectly
        // readable, and "could not be read" would send somebody looking at
        // their permissions instead of at their edits.
        const QFileInfo info(failure.sourcePath);
        if (static_cast<quint64>(info.size()) != entry->size) {
            failure.obstacle = RepairObstacle::SourceChanged;
            failure.detail =
                QCoreApplication::translate(
                    "Repair",
                    "It was %1 bytes when it was captured and it is %2 now, so it cannot stand in "
                    "for the copy in the archive. Capture again instead.")
                    .arg(entry->size)
                    .arg(info.size());
            report.failures.push_back(failure);
            continue;
        }

        auto content = consistent_copy::readFile(failure.sourcePath, entry->size);
        if (!content) {
            failure.obstacle = RepairObstacle::SourceUnreadable;
            failure.detail = describeError(content.error());
            report.failures.push_back(failure);
            continue;
        }

        // The whole point of the check: a file that has been edited since the
        // capture cannot repair that capture. Putting the new version in would
        // produce an archive that passes every check and does not hold what it
        // says it holds.
        const format::ContentDigests digests = format::hashContent(toByteView(*content));
        if (digests.blake2b != entry->contentHash) {
            failure.obstacle = RepairObstacle::SourceChanged;
            failure.detail = QCoreApplication::translate(
                "Repair",
                "The copy on this machine is not the one that was captured, so it cannot stand "
                "in for it. Capture again instead.");
            report.failures.push_back(failure);
            continue;
        }

        recovered.push_back(Recovered{entry, std::move(*content)});
    }

    if (recovered.empty()) {
        report.succeeded = false;
        report.errorMessage =
            QCoreApplication::translate(
                "Repair", "None of the %1 damaged file(s) could be recovered from this machine.")
                .arg(report.filesNeedingRepair);
        return report;
    }

    // ------------------------------------------------------- write it out
    const std::filesystem::path repairPath =
        format::ArchiveReader::repairPathFor(reader->parts().front());

    format::ArchiveOptions options;
    options.preset = format::CompressionPreset::Fast;
    options.passphrase = toUtf8(request.passphrase);
    options.syncIntervalBytes = 0;
    options.recordMd5 = true;

    auto writing = format::ArchiveWriter::create(repairPath, options);
    if (!writing) {
        return fail(std::move(report), describeError(writing.error()));
    }
    auto writer = std::move(writing).value();

    format::Manifest repairManifest;
    repairManifest.archiveId = (*manifest)->archiveId;
    repairManifest.label = QStringLiteral("repair").toStdString();
    repairManifest.source = (*manifest)->source;
    repairManifest.preset = format::CompressionPreset::Fast;
    repairManifest.encrypted = writer->isEncrypted();

    format::BlockPacker packer(format::kDefaultSolidBlockSize,
                               [&writer](format::ByteView raw) -> format::Result<std::uint32_t> {
                                   const std::uint32_t blockId = writer->nextBlockId();
                                   TRANSMIT_TRY(prepared, writer->prepare(blockId, raw));
                                   TRANSMIT_CHECK(writer->writePrepared(prepared));
                                   return blockId;
                               });

    std::vector<format::BlockPacker::PlacementId> handles;
    for (const Recovered& item : recovered) {
        // The archive's own record of the file, carried over unchanged: the
        // repair stands in for this entry, so it has to describe it the same
        // way, down to the recorded hashes a reader checks it against.
        format::ManifestEntry entry = *item.entry;
        entry.location = {};

        auto handle = packer.add(entry.contentHash, toByteView(item.content));
        if (!handle) {
            return fail(std::move(report), describeError(handle.error()));
        }
        handles.push_back(*handle);
        repairManifest.entries.push_back(entry);
    }

    if (const auto flushed = packer.flush(); !flushed) {
        return fail(std::move(report), describeError(flushed.error()));
    }
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const auto location = packer.location(handles[i]);
        if (!location) {
            return fail(std::move(report), describeError(location.error()));
        }
        repairManifest.entries[i].location = *location;
    }

    if (const auto finished = writer->finish(repairManifest); !finished) {
        return fail(std::move(report), describeError(finished.error()));
    }

    report.repairPath = fromUtf8(format::fromFsPath(repairPath));
    report.filesRepaired = static_cast<quint64>(recovered.size());

    // Attached and read back, so the answer is "the archive now reads
    // correctly" rather than "a file was written and we hope".
    auto rechecking = format::ArchiveReader::open(archivePath);
    if (!rechecking) {
        return fail(std::move(report), describeError(rechecking.error()));
    }
    auto rechecked = std::move(rechecking).value();
    if (rechecked->isEncrypted()) {
        if (const auto unlocked = rechecked->unlock(toUtf8(request.passphrase)); !unlocked) {
            return fail(std::move(report), describeError(unlocked.error()));
        }
        if (const auto attached = rechecked->attachRepair(repairPath, toUtf8(request.passphrase));
            !attached) {
            return fail(std::move(report), describeError(attached.error()));
        }
    }
    const auto recheckedManifest = rechecked->manifest();
    if (!recheckedManifest) {
        return fail(std::move(report), describeError(recheckedManifest.error()));
    }
    if (!rechecked->hasRepair()) {
        return fail(std::move(report),
                    QCoreApplication::translate(
                        "Repair", "The repair was written but this archive will not use it."));
    }

    quint64 confirmed = 0;
    for (const format::ManifestEntry& entry : (*recheckedManifest)->entries) {
        if (!entry.hasContent() || !asked.contains(fromUtf8(entry.path.toDisplayString()))) {
            continue;
        }
        if (rechecked->readEntry(entry)) {
            ++confirmed;
        }
    }

    report.succeeded = true;
    qCInfo(logRestore) << "repaired" << report.filesRepaired << "file(s)," << report.failures.size()
                       << "could not be recovered," << confirmed << "read back correctly";
    return report;
}

}  // namespace transmit::core
