#include "core/services/ExportService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <vector>

#include "core/recipe/AppInventoryPayload.h"
#include "core/recipe/RecipeCatalog.h"
#include "core/secrets/SecretsDomain.h"
#include "core/services/ConsistentCopy.h"
#include "core/services/VerifyService.h"
#include "core/settings/SettingsDomain.h"
#include "core/tasks/BlockPipeline.h"
#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/BlockPacker.h"
#include "format/ChecksumSidecar.h"
#include "format/Container.h"
#include "format/Serialization.h"
#include "format/hash/ContentHash.h"

namespace transmit::core {
namespace {

/// Nanoseconds since a mark. Used inside the capture loop, where a scoped
/// timer would measure its own construction thousands of times over.
qint64 elapsedNanoseconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                                since)
        .count();
}

constexpr qint64 kProgressIntervalMs = 100;

/// How much payload may sit unwritten in the page cache on removable media.
/// Small enough that pulling the stick loses seconds rather than minutes, and
/// that a full stick says so early; large enough that the flush itself is a
/// rounding error next to the write it follows.
constexpr quint64 kRemovableSyncIntervalBytes = 32ULL * 1024 * 1024;

/// The volume a path is on, or a default-constructed one when nothing matches
/// - which is the honest answer for a path on a drive the platform layer does
/// not enumerate, and treats it as fixed rather than removable.
platform::StorageVolume volumeFor(const platform::PlatformService& platform, const QString& path) {
    const QString absolute = QFileInfo(path).absoluteFilePath();
    platform::StorageVolume best;
    qsizetype bestLength = -1;
    for (const platform::StorageVolume& volume : platform.storageVolumes()) {
        const QString root = QDir::cleanPath(volume.rootPath);
        if (root.isEmpty() || !absolute.startsWith(root)) {
            continue;
        }
        // The longest matching root wins, so "/media/usb" is preferred over
        // "/" for a path that is under both.
        if (root.length() > bestLength) {
            bestLength = root.length();
            best = volume;
        }
    }
    return best;
}

/// Orders items so similar content lands in the same solid block. Grouping by
/// extension first puts all the JSON, all the PNGs and all the source files
/// together, which is where most of the compression gain comes from; the path
/// is the tie-break so a directory stays contiguous.
///
/// The keys are worked out once per file rather than inside the comparator.
/// The comparator used to build two QFileInfo objects every time it was
/// called - and a sort calls it n log n times, so a home directory of half a
/// million files meant ten million of them, all to look at the letters after
/// the last dot.
void sortForCompression(QList<ScannedItem>& items) {
    if (items.size() < 2) {
        return;
    }

    struct Key {
        QString extension;
        const std::string* relative;
        qsizetype index;
    };

    std::vector<Key> keys;
    keys.reserve(static_cast<std::size_t>(items.size()));
    for (qsizetype i = 0; i < items.size(); ++i) {
        keys.push_back(Key{fileExtension(items[i].absolutePath), &items[i].tokenPath.relative, i});
    }

    // Sorting the keys rather than the items also stops the sort moving
    // ScannedItem around, which is a dozen strings each.
    std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) {
        if (a.extension != b.extension) {
            return a.extension < b.extension;
        }
        return *a.relative < *b.relative;
    });

    QList<ScannedItem> ordered;
    ordered.reserve(items.size());
    for (const Key& key : keys) {
        ordered.push_back(std::move(items[key.index]));
    }
    items = std::move(ordered);
}

format::ManifestEntry toManifestEntry(const ScannedItem& item, quint64 id) {
    format::ManifestEntry entry;
    entry.id = id;
    entry.domain = item.domain;
    entry.type = item.type;
    entry.path = item.tokenPath;
    entry.size = item.size;
    entry.modifiedUnixNs = item.modifiedUnixNs;
    entry.createdUnixNs = item.createdUnixNs;
    entry.posix = item.posix;
    entry.windows = item.windows;
    entry.symlinkTarget = toUtf8(item.symlinkTarget);
    entry.appId = toUtf8(item.appId);
    return entry;
}

}  // namespace

ExportService::ExportService(platform::PlatformService& platformService)
    : platform_(platformService) {}

QList<platform::RunningApp> ExportService::applicationsToClose(
    const CaptureSelection& selection) const {
    if (!selection.includes(DomainId::AppState)) {
        return {};
    }

    RecipeCatalog catalog;
    catalog.loadDefaults();

    const platform::EnvironmentInfo environment = platform_.environment();
    const format::PathTokenMap folders = platform_.knownFolders();

    QList<MatchedApp> matched = catalog.match(platform_.installedApplications(), environment.os);
    matched += catalog.matchByStateOnly(matched, environment.os, folders);

    QStringList quiesce;
    for (const MatchedApp& match : matched) {
        // Only the applications whose data is actually being taken. Asking
        // somebody to close a browser whose profile was deselected is asking
        // them to stop work for nothing, and it teaches them to ignore the
        // list - including the entries that mattered.
        if (selection.capturesStateOf(match.recipe.id)) {
            quiesce += match.recipe.quiesceProcesses;
        }
    }
    return platform_.runningApplications(quiesce);
}

quint64 ExportService::splitSizeFor(const platform::StorageVolume& volume) {
    if (!volume.requiresSplitting()) {
        return 0;
    }
    return format::kFat32SafePartSize;
}

quint64 ExportService::estimateSize(const CaptureSelection& selection,
                                    CancelToken& cancelToken) const {
    const ScanService scanner(platform_);
    return scanner.scan(selection, cancelToken).totalBytes;
}

ExportReport ExportService::run(const ExportRequest& request, CancelToken& cancelToken,
                                const ProgressCallback& progress) {
    ExportReport report;
    QElapsedTimer timer;
    timer.start();

    // Every stage of the run named and measured. It costs one steady_clock
    // reading per stage - nothing measurable against the work between them -
    // and it is what turns "the capture was slow" into a thing somebody can
    // do something about.
    StageTimer stages;

    // Clears away what a failed capture wrote.
    //
    // A capture that stops half way leaves an archive that cannot be restored
    // from, and one that looks exactly like a good one until somebody carries
    // it to another machine and tries. Two things decide when removing it is
    // safe, and both are why this is a guard rather than a few lines inside
    // `fail`:
    //
    //   - Not before the writer exists. Until then the destination may still
    //     hold a perfectly good archive from an earlier run that this one has
    //     not touched; creating the writer is what truncates it.
    //   - Not while the writer exists. Windows refuses to delete a file that
    //     is still open, so removing from inside `fail` - with the writer
    //     alive until `run` returns - would leave behind exactly the
    //     half-written archive it means to clear away. Declared before the
    //     writer, this runs after it.
    struct PartialArchiveCleanup {
        std::vector<std::filesystem::path> parts;

        ~PartialArchiveCleanup() {
            for (const std::filesystem::path& part : parts) {
                std::error_code ignored;
                std::filesystem::remove(part, ignored);
            }
        }
    } cleanup;

    const std::vector<std::filesystem::path>* writtenParts = nullptr;

    const auto fail = [&report, &timer, &writtenParts, &cleanup](const QString& message) {
        report.succeeded = false;
        report.errorMessage = message;
        report.elapsedMilliseconds = timer.elapsed();

        if (writtenParts != nullptr) {
            cleanup.parts = *writtenParts;
        }

        qCWarning(logCapture) << "capture failed:" << message;
        return report;
    };

    if (request.destinationPath.isEmpty()) {
        return fail(QCoreApplication::translate("Export", "No destination was chosen."));
    }

    // Credentials must never reach removable media unprotected.
    if (request.selection.includes(DomainId::Secrets) && request.passphrase.isEmpty()) {
        return fail(QCoreApplication::translate(
            "Export",
            "Credentials were selected, so the archive must be encrypted. Set a passphrase or "
            "clear the credentials option."));
    }

    const platform::EnvironmentInfo environment = platform_.environment();

    // ------------------------------------------------- application state
    // Program binaries cannot cross an operating system boundary, but the data
    // and settings they keep can. The catalog says where each application puts
    // that state on this platform, and those directories join the selection.
    CaptureSelection selection = request.selection;
    QList<MatchedApp> matchedApps;

    if (selection.includes(DomainId::AppState) || selection.includes(DomainId::AppInventory)) {
        if (progress) {
            ProgressUpdate update;
            update.stage = QCoreApplication::translate("Export", "Looking at your programs");
            update.phase = ProgressPhase::Preparing;
            progress(update);
        }

        RecipeCatalog catalog;
        catalog.loadDefaults();

        const format::PathTokenMap folders = platform_.knownFolders();
        const QList<platform::InstalledApp> installed = platform_.installedApplications();

        matchedApps = catalog.match(installed, environment.os);
        matchedApps += catalog.matchByStateOnly(matchedApps, environment.os, folders);
        catalog.noteWhichHaveState(matchedApps, environment.os, folders);

        if (selection.includes(DomainId::AppState)) {
            selection.roots +=
                catalog.captureRootsFor(matchedApps, environment.os, folders, &selection);
        }

        // What the person chose not to carry is still recorded as installed
        // unless they said otherwise, because the list costs a few hundred
        // bytes for the whole machine and is the only thing that makes a
        // restore able to offer anything at all.
        QList<MatchedApp> forInventory;
        for (const MatchedApp& match : matchedApps) {
            if (selection.answerFor(match.recipe.id).recordForReinstall) {
                forInventory.push_back(match);
            }
        }
        matchedApps = forInventory;

        // Anything holding its files open would be captured mid-write, so the
        // user is told which programs to close rather than having them killed.
        QStringList quiesce;
        for (const MatchedApp& match : matchedApps) {
            quiesce += match.recipe.quiesceProcesses;
        }
        for (const platform::RunningApp& running : platform_.runningApplications(quiesce)) {
            report.notes.push_back(ContinuityNote{
                ContinuityGrade::Manual, DomainId::AppState, running.displayName,
                QCoreApplication::translate(
                    "Export",
                    "This program is running, so its data may be captured half-written. Close it "
                    "and run the capture again for a clean copy.")});
        }

        for (const MatchedApp& match : matchedApps) {
            if (!match.recipe.note.isEmpty()) {
                report.notes.push_back(ContinuityNote{match.recipe.expectedGrade,
                                                      DomainId::AppState, match.recipe.displayName,
                                                      match.recipe.note});
            }
        }
        qCInfo(logCapture) << "recognised" << matchedApps.size() << "applications";
    }

    // ------------------------------------------------- saved passwords
    // Refused above unless the archive is encrypted, so by this point the
    // plaintext has somewhere safe to go.
    format::ByteBuffer secretsPayload;
    if (selection.includes(DomainId::Secrets)) {
        if (progress) {
            ProgressUpdate update;
            update.stage = QCoreApplication::translate("Export", "Collecting saved passwords");
            update.phase = ProgressPhase::Preparing;
            progress(update);
        }

        const SecretsDomain secrets(platform_);
        SecretsDomain::CaptureOptions options;
        auto captured = secrets.capture(options);

        secretsPayload = std::move(captured.payload);
        report.notes += captured.notes;
    }

    // --------------------------------------------------- system settings
    QList<CapturedSetting> capturedSettings;
    if (selection.includes(DomainId::SystemSettings)) {
        const SettingsDomain settings(platform_);
        capturedSettings = settings.capture();

        // A wallpaper setting is worthless if the image stayed behind, so the
        // picture itself joins the capture.
        const QString wallpaper = SettingsDomain::wallpaperPath(capturedSettings);
        if (!wallpaper.isEmpty() && QFileInfo::exists(wallpaper)) {
            const format::TokenizedPath tokenised =
                platform_.knownFolders().tokenize(toUtf8(wallpaper));
            if (!tokenised.isAbsoluteFallback()) {
                CaptureRoot root;
                root.token = tokenised.token;
                root.relative = fromUtf8(tokenised.relative);
                root.domain = DomainId::SystemSettings;
                root.recursive = false;
                selection.roots.push_back(root);
            } else {
                report.notes.push_back(ContinuityNote{
                    ContinuityGrade::Manual, DomainId::SystemSettings,
                    QCoreApplication::translate("Export", "Desktop background"),
                    QCoreApplication::translate(
                        "Export",
                        "The image is outside your personal folders, so it was not captured. "
                        "Copy \"%1\" across yourself if you want it.")
                        .arg(wallpaper)});
            }
        }
    }

    // ------------------------------------------------------------ scan
    if (progress) {
        ProgressUpdate update;
        update.stage = QCoreApplication::translate("Export", "Looking through your files");
        update.phase = ProgressPhase::Scanning;
        progress(update);
    }

    stages.begin(QStringLiteral("scan"));
    const ScanService scanner(platform_);
    ScanResult scan = scanner.scan(selection, cancelToken, progress);
    stages.end();
    report.notes += scan.notes;
    report.incomplete = scan.incomplete();
    report.unreadablePaths = scan.unreadableDirectories;

    if (cancelToken.isCancelled()) {
        return fail(QCoreApplication::translate("Export", "Cancelled."));
    }

    // ------------------------------------------------------- snapshot
    QStringList snapshotPaths;
    for (const ScannedItem& item : scan.items) {
        if (item.type == format::EntryType::Directory) {
            snapshotPaths.push_back(item.absolutePath);
            break;
        }
    }
    stages.begin(QStringLiteral("snapshot"));
    const std::unique_ptr<platform::Snapshot> snapshot = platform_.createSnapshot(snapshotPaths);
    stages.end();
    if (!snapshot->isRealSnapshot() && !snapshot->unavailableReason().isEmpty()) {
        report.notes.push_back(
            ContinuityNote{ContinuityGrade::Adapted, DomainId::Unknown,
                           QCoreApplication::translate("Export", "Filesystem snapshot"),
                           snapshot->unavailableReason()});
    }

    stages.begin(QStringLiteral("sort"));
    sortForCompression(scan.items);
    stages.end();

    // ----------------------------------------------------- open archive
    format::ArchiveOptions options;
    options.preset = request.packaging.preset;
    options.partSize = request.packaging.partSize;
    options.passphrase = toUtf8(request.passphrase);
    options.solidBlockSize = request.packaging.solidBlockSize;
    options.recordMd5 = request.packaging.recordMd5;

    const platform::StorageVolume destination = volumeFor(platform_, request.destinationPath);

    // ------------------------------------------------------- can it go there
    //
    // Asked here, after the scan and before a single byte is written: a stick
    // that is write-protected, or plainly too small, should say so now rather
    // than at ninety-eight percent of an hour's work.
    if (destination.readOnly) {
        return fail(
            QCoreApplication::translate(
                "Export",
                "%1 is write-protected, so nothing can be written to it. Some drives have a "
                "small switch on the side; otherwise choose somewhere else.")
                .arg(destination.displayName.isEmpty()
                         ? QDir::toNativeSeparators(request.destinationPath)
                         : destination.displayName));
    }

    // Only when the platform actually knows about this volume - a
    // default-constructed one means "a drive we do not enumerate", and refusing
    // on a number nobody measured would be worse than trying.
    if (destination.totalBytes > 0 && scan.totalBytes > 0) {
        // The archive is compressed, so the uncompressed total is an upper
        // bound rather than a requirement, and refusing on it would turn away
        // captures that would have fitted comfortably. What no compression
        // saves is ninety-five percent of a mixed home directory, so that is
        // where the refusal sits; between the two, the run goes ahead and the
        // report says it was foreseeable.
        const quint64 hopeless = scan.totalBytes / 20;
        if (destination.freeBytes < hopeless) {
            return fail(QCoreApplication::translate(
                            "Export",
                            "There is %1 free on %2 and %3 to copy. Even compressed, that will "
                            "not fit.")
                            .arg(formatBytes(destination.freeBytes),
                                 destination.displayName.isEmpty()
                                     ? QDir::toNativeSeparators(request.destinationPath)
                                     : destination.displayName,
                                 formatBytes(scan.totalBytes)));
        }
        if (destination.freeBytes < scan.totalBytes) {
            report.notes.push_back(ContinuityNote{
                ContinuityGrade::Manual, DomainId::Unknown,
                QCoreApplication::translate("Export", "Space on the drive"),
                QCoreApplication::translate(
                    "Export",
                    "There is %1 free and %2 to copy. It may still fit once compressed, but "
                    "if it does not, the capture will stop part way.")
                    .arg(formatBytes(destination.freeBytes), formatBytes(scan.totalBytes))});
        }
    }

    // On a USB stick the operating system will happily accept gigabytes it has
    // not written yet, so without this the archive looks finished long before
    // it is, a full stick reports ENOSPC only at close, and pulling one out
    // loses far more than the last moment's work. Flushing every 32 MiB costs
    // a fraction of a second per interval on any stick worth using and keeps
    // all three of those honest. Internal disks are left alone: there the
    // page cache is the whole point.
    if (request.packaging.syncIntervalBytes > 0) {
        options.syncIntervalBytes = request.packaging.syncIntervalBytes;
    } else if (destination.removable) {
        options.syncIntervalBytes = kRemovableSyncIntervalBytes;
    }

    // A folder that is not there yet is not a refusal: somebody who typed
    // "/media/usb/backups/laptop.txa" has said where they want it.
    const QDir folder = QFileInfo(request.destinationPath).absoluteDir();
    if (!folder.exists()) {
        // Only one level is made, and only inside a folder that already
        // exists, so a mistyped path still fails instead of being built out.
        // Taken apart as a string rather than with cdUp(), which refuses to
        // move to a directory that is not there - which is the case being
        // reported on.
        const QDir parent(QFileInfo(folder.absolutePath()).absolutePath());
        if (!parent.exists()) {
            return fail(QCoreApplication::translate(
                            "Export", "There is no folder at %1 to write the archive into.")
                            .arg(QDir::toNativeSeparators(parent.absolutePath())));
        }
        if (!parent.mkdir(folder.dirName())) {
            return fail(QCoreApplication::translate("Export", "Could not make the folder %1.")
                            .arg(QDir::toNativeSeparators(folder.absolutePath())));
        }
    }

    auto writerResult =
        format::ArchiveWriter::create(format::toFsPath(toUtf8(request.destinationPath)), options);
    if (!writerResult) {
        return fail(describeError(writerResult.error()));
    }
    auto writer = std::move(writerResult).value();
    writtenParts = &writer->parts();

    BlockPipeline pipeline(*writer, request.packaging.workerCount);
    pipeline.setAbortCheck([&cancelToken] { return cancelToken.isCancelled(); });
    format::BlockPacker packer(request.packaging.solidBlockSize,
                               [&pipeline](format::ByteView raw) -> format::Result<quint32> {
                                   return pipeline.submit(raw);
                               });

    // ---------------------------------------------------------- capture
    format::Manifest manifest;
    manifest.label = toUtf8(request.label);

    manifest.source.os = environment.os;
    manifest.source.osName = toUtf8(environment.osName);
    manifest.source.osVersion = toUtf8(environment.osVersion);
    manifest.source.distroId = toUtf8(environment.distroId);
    manifest.source.desktopEnvironment = toUtf8(environment.desktopEnvironment);
    manifest.source.hostName = toUtf8(environment.hostName);
    manifest.source.userName = toUtf8(environment.userName);
    manifest.source.homeDirectory = toUtf8(environment.homeDirectory);
    manifest.source.appVersion = QStringLiteral(TRANSMIT_VERSION).toStdString();
    manifest.source.capturedUnix = QDateTime::currentSecsSinceEpoch();

    // Recording the source machine's folder layout lets the restore recognise
    // absolute paths that turn up inside configuration files.
    const format::PathTokenMap sourceTokens = platform_.knownFolders();
    for (const format::PathTokenId token : format::allTokens()) {
        if (const auto base = sourceTokens.base(token)) {
            manifest.source.tokenBases[token] = *base;
        }
    }

    if (!secretsPayload.empty()) {
        manifest.payloads.push_back(
            format::DomainPayload{DomainId::Secrets, "secrets.v1", std::move(secretsPayload)});
    }

    if (!capturedSettings.isEmpty()) {
        manifest.payloads.push_back(format::DomainPayload{
            DomainId::SystemSettings, "settings.v1", SettingsDomain::encode(capturedSettings)});
    }

    if (!matchedApps.isEmpty()) {
        manifest.payloads.push_back(format::DomainPayload{DomainId::AppInventory, "apps.v1",
                                                          encodeAppInventory(matchedApps)});
    }

    QList<format::BlockPacker::PlacementId> placements;
    QList<qsizetype> placementEntryIndex;

    QElapsedTimer throttle;
    throttle.start();

    quint64 nextId = 1;
    quint64 bytesDone = 0;
    quint64 filesDone = 0;
    bool reportedProgress = false;

    for (const ScannedItem& item : scan.items) {
        if (cancelToken.isCancelled()) {
            return fail(QCoreApplication::translate("Export", "Cancelled."));
        }

        format::ManifestEntry entry = toManifestEntry(item, nextId++);

        if (item.type == format::EntryType::File && item.size > 0) {
            const QString readPath = snapshot->translate(item.absolutePath);

            // Timed by hand rather than with a Scope: three stages run inside
            // this loop thousands of times over, and a stage object per file
            // would be measuring its own construction.
            const auto readStarted = std::chrono::steady_clock::now();
            auto content = consistent_copy::readFile(readPath, item.size);
            stages.add(QStringLiteral("read"), elapsedNanoseconds(readStarted));

            if (!content) {
                // A file that vanished or locked mid-capture is reported rather
                // than aborting the whole run.
                report.notes.push_back(ContinuityNote{
                    ContinuityGrade::Manual, item.domain, item.absolutePath,
                    QCoreApplication::translate("Export", "Could not be captured: %1")
                        .arg(describeError(content.error()))});
                continue;
            }

            const format::ByteView bytes = toByteView(*content);
            entry.size = static_cast<quint64>(bytes.size());

            // Both digests in one walk of the buffer. They answer different
            // questions: the BLAKE2b is the identity the packer deduplicates
            // on, the MD5 is what somebody can check with md5sum later.
            const auto hashStarted = std::chrono::steady_clock::now();
            const format::ContentDigests digests =
                format::hashContent(bytes, request.packaging.recordMd5);
            stages.add(QStringLiteral("hash"), elapsedNanoseconds(hashStarted));

            entry.contentHash = digests.blake2b;
            entry.contentMd5 = digests.md5;

            // Packing includes waiting for a compression worker when they are
            // all busy, which is exactly the number that says whether the
            // machine is short of workers or short of disk.
            const auto packStarted = std::chrono::steady_clock::now();
            auto handle = packer.add(entry.contentHash, bytes);
            stages.add(QStringLiteral("pack"), elapsedNanoseconds(packStarted));
            if (!handle) {
                return fail(describeError(handle.error()));
            }
            placements.push_back(*handle);
            placementEntryIndex.push_back(static_cast<qsizetype>(manifest.entries.size()));

            bytesDone += entry.size;
            ++filesDone;
        }

        manifest.entries.push_back(std::move(entry));

        // The first item reports straight away: the throttle would otherwise
        // hold every update back by an interval, which on a small capture is
        // long enough for the whole run to finish while the window still says
        // it is looking through files.
        if (progress && (!reportedProgress || throttle.elapsed() >= kProgressIntervalMs)) {
            throttle.restart();
            reportedProgress = true;
            ProgressUpdate update;
            update.filesDone = filesDone;
            update.filesTotal = scan.fileCount;
            update.bytesDone = bytesDone;
            update.bytesTotal = scan.totalBytes;
            update.bytesStored = writer->storedBytes();
            update.currentItem = item.absolutePath;
            update.stage = QCoreApplication::translate("Export", "Compressing");
            update.phase = ProgressPhase::Transferring;
            progress(update);
        }
    }

    if (const auto status = packer.flush(); !status) {
        return fail(describeError(status.error()));
    }
    if (const auto status = pipeline.drain([&cancelToken] { return cancelToken.isCancelled(); });
        !status) {
        return fail(describeError(status.error()));
    }
    // A drain that stopped early leaves blocks the manifest would point at but
    // the archive does not hold, so this is checked before anything is written
    // that would make the file look complete.
    if (cancelToken.isCancelled()) {
        return fail(QCoreApplication::translate("Export", "Cancelled."));
    }

    // Block ids are only final once their block has been written, so the
    // manifest entries are patched here rather than during the walk.
    for (qsizetype i = 0; i < placements.size(); ++i) {
        const auto location = packer.location(placements[i]);
        if (!location) {
            return fail(describeError(location.error()));
        }
        manifest.entries[static_cast<std::size_t>(placementEntryIndex[i])].location = *location;
    }
    manifest.deduplicatedBytes = packer.deduplicatedBytes();

    // The manifest, the footer, patching every part header and the fsync that
    // makes all of it real. On a stick this is often the longest stage and
    // never the one anybody expects.
    stages.begin(QStringLiteral("finish"));
    const auto finishStatus = writer->finish(manifest);
    stages.end();
    if (!finishStatus) {
        return fail(describeError(finishStatus.error()));
    }

    // ----------------------------------------------------------- report
    report.succeeded = true;
    report.archiveId = fromUtf8(manifest.archiveId);
    report.encrypted = writer->isEncrypted();
    report.fileCount = scan.fileCount;
    report.directoryCount = scan.directoryCount;
    report.symlinkCount = scan.symlinkCount;
    report.rawBytes = bytesDone;
    report.storedBytes = writer->storedBytes();
    report.deduplicatedBytes = packer.deduplicatedBytes();

    for (const auto& part : writer->parts()) {
        report.archiveParts.push_back(fromUtf8(format::fromFsPath(part)));
    }

    // Written after finish(), so it hashes the parts as they will be read -
    // including the patched headers that finish() goes back and writes.
    if (request.packaging.writeMd5Sidecar && request.packaging.recordMd5) {
        const std::filesystem::path sidecarPath =
            format::toFsPath(toUtf8(request.destinationPath + QStringLiteral(".md5")));

        format::SidecarOptions sidecar;
        sidecar.archiveName =
            format::fromFsPath(format::toFsPath(toUtf8(request.destinationPath)).filename());
        sidecar.includeEntries =
            !writer->isEncrypted() || request.packaging.sidecarNamesEvenWhenEncrypted;

        stages.begin(QStringLiteral("sidecar"));
        const auto written =
            format::writeChecksumSidecar(sidecarPath, writer->parts(), manifest, sidecar);
        stages.end();
        if (written) {
            report.checksumSidecar = fromUtf8(format::fromFsPath(*written));
        } else {
            // The archive is written and sound. Losing the convenience of an
            // external checksum file is not a reason to tell somebody their
            // capture failed, so it is a note rather than an error.
            report.notes.push_back(ContinuityNote{
                ContinuityGrade::Manual, format::DomainId::Unknown, report.archiveParts.value(0),
                QCoreApplication::translate("Export",
                                            "The archive is complete, but its checksum file "
                                            "could not be written: %1")
                    .arg(describeError(written.error()))});
        }
    }

    // ----------------------------------------------------------- verify
    //
    // Here rather than left to somebody to remember: an archive that did not
    // survive the journey onto the drive is not a capture that worked, and the
    // moment to find that out is while the machine it came from still exists.
    if (request.packaging.verifyAfterWriting) {
        VerifyRequest verification;

        // A part rather than the base name: a split archive has no file at
        // `name.txa` at all, and opening it would fail with "no such file"
        // after a capture that went perfectly.
        verification.archivePath = report.archiveParts.value(0, request.destinationPath);
        verification.sidecarPath = report.checksumSidecar;
        verification.useSidecar = !report.checksumSidecar.isEmpty();
        verification.passphrase = request.passphrase;
        verification.deep = true;

        stages.begin(QStringLiteral("verify"));
        const VerifyService verifier(platform_);
        const VerifyReport verified = verifier.run(verification, cancelToken, progress);
        stages.end();

        report.verificationRan = true;
        report.verified = verified.everythingMatched();
        report.verificationUsedColdReads = verified.cacheDropped;
        report.verifiedFiles = verified.filesChecked;
        report.verificationFailures = verified.filesFailed;
        report.verificationRetriedReads = verified.retriedReads;
        report.verificationMilliseconds = verified.elapsedMilliseconds;

        if (!verified.cacheDropped) {
            report.notes.push_back(ContinuityNote{
                ContinuityGrade::Adapted, format::DomainId::Unknown,
                QCoreApplication::translate("Export", "Verification"),
                QCoreApplication::translate(
                    "Export",
                    "This system cannot be asked to forget its cached copy of a file, so the "
                    "read-back may have come from memory rather than from the drive.")});
        }
        for (const VerifyFileResult& failure : verified.failures) {
            report.notes.push_back(ContinuityNote{
                ContinuityGrade::Impossible, format::DomainId::Unknown, failure.path,
                QCoreApplication::translate("Export", "Did not come back off the drive: %1")
                    .arg(failure.detail)});
        }
        if (verified.retriedReads > 0) {
            report.notes.push_back(ContinuityNote{
                ContinuityGrade::Adapted, format::DomainId::Unknown,
                QCoreApplication::translate("Export", "Verification"),
                QCoreApplication::translate(
                    "Export",
                    "%1 read(s) only worked after retrying. The archive is sound, but a drive "
                    "that needs a second attempt is worth replacing.")
                    .arg(verified.retriedReads)});
        }

        if (!report.verified) {
            report.succeeded = false;
            report.errorMessage =
                verified.errorMessage.isEmpty()
                    ? QCoreApplication::translate(
                          "Export", "%1 of %2 files did not read back from the drive correctly.")
                          .arg(verified.filesFailed)
                          .arg(verified.filesChecked)
                    : verified.errorMessage;
            qCWarning(logCapture) << "verification failed:" << report.errorMessage;
            return report;
        }
    }

    // Set here rather than with the rest of the report: the read-back, the
    // checksum file and the part patching all happen after that point, and a
    // capture that took three and a half seconds should not tell somebody it
    // took one and a half.
    report.elapsedMilliseconds = timer.elapsed();
    report.stages = stages.stages();

    qCInfo(logCapture) << "captured" << report.fileCount << "files:" << formatBytes(report.rawBytes)
                       << "->" << formatBytes(report.storedBytes) << "in"
                       << report.elapsedMilliseconds << "ms";
    qCInfo(logCapture) << "time went:" << stages.summary();
    return report;
}

}  // namespace transmit::core
