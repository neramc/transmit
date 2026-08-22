#include "core/services/ExportService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>

#include "core/recipe/AppInventoryPayload.h"
#include "core/recipe/RecipeCatalog.h"
#include "core/secrets/SecretsDomain.h"
#include "core/services/ConsistentCopy.h"
#include "core/settings/SettingsDomain.h"
#include "core/tasks/BlockPipeline.h"
#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/BlockPacker.h"
#include "format/Container.h"
#include "format/Serialization.h"

namespace transmit::core {
namespace {

constexpr qint64 kProgressIntervalMs = 100;

/// Orders items so similar content lands in the same solid block. Grouping by
/// extension first puts all the JSON, all the PNGs and all the source files
/// together, which is where most of the compression gain comes from; the path
/// is the tie-break so a directory stays contiguous.
void sortForCompression(QList<ScannedItem>& items) {
    std::stable_sort(items.begin(), items.end(), [](const ScannedItem& a, const ScannedItem& b) {
        const QString extensionA = QFileInfo(a.absolutePath).suffix().toLower();
        const QString extensionB = QFileInfo(b.absolutePath).suffix().toLower();
        if (extensionA != extensionB) {
            return extensionA < extensionB;
        }
        return a.tokenPath.relative < b.tokenPath.relative;
    });
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
        quiesce += match.recipe.quiesceProcesses;
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

    const auto fail = [&report, &timer](const QString& message) {
        report.succeeded = false;
        report.errorMessage = message;
        report.elapsedMilliseconds = timer.elapsed();
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
            progress(update);
        }

        RecipeCatalog catalog;
        catalog.loadDefaults();

        const format::PathTokenMap folders = platform_.knownFolders();
        const QList<platform::InstalledApp> installed = platform_.installedApplications();

        matchedApps = catalog.match(installed, environment.os);
        matchedApps += catalog.matchByStateOnly(matchedApps, environment.os, folders);

        if (selection.includes(DomainId::AppState)) {
            selection.roots += catalog.captureRootsFor(matchedApps, environment.os, folders);
        }

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
        progress(update);
    }

    const ScanService scanner(platform_);
    ScanResult scan = scanner.scan(selection, cancelToken, progress);
    report.notes += scan.notes;

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
    const std::unique_ptr<platform::Snapshot> snapshot = platform_.createSnapshot(snapshotPaths);
    if (!snapshot->isRealSnapshot() && !snapshot->unavailableReason().isEmpty()) {
        report.notes.push_back(
            ContinuityNote{ContinuityGrade::Adapted, DomainId::Unknown,
                           QCoreApplication::translate("Export", "Filesystem snapshot"),
                           snapshot->unavailableReason()});
    }

    sortForCompression(scan.items);

    // ----------------------------------------------------- open archive
    format::ArchiveOptions options;
    options.preset = request.preset;
    options.partSize = request.partSize;
    options.passphrase = toUtf8(request.passphrase);
    options.solidBlockSize = request.solidBlockSize;

    auto writerResult =
        format::ArchiveWriter::create(format::toFsPath(toUtf8(request.destinationPath)), options);
    if (!writerResult) {
        return fail(describeError(writerResult.error()));
    }
    auto writer = std::move(writerResult).value();

    BlockPipeline pipeline(*writer);
    format::BlockPacker packer(request.solidBlockSize,
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

    for (const ScannedItem& item : scan.items) {
        if (cancelToken.isCancelled()) {
            return fail(QCoreApplication::translate("Export", "Cancelled."));
        }

        format::ManifestEntry entry = toManifestEntry(item, nextId++);

        if (item.type == format::EntryType::File && item.size > 0) {
            const QString readPath = snapshot->translate(item.absolutePath);
            auto content = consistent_copy::readFile(readPath, item.size);

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
            entry.contentHash = format::Blake2b::hash256(bytes);

            auto handle = packer.add(entry.contentHash, bytes);
            if (!handle) {
                return fail(describeError(handle.error()));
            }
            placements.push_back(*handle);
            placementEntryIndex.push_back(static_cast<qsizetype>(manifest.entries.size()));

            bytesDone += entry.size;
            ++filesDone;
        }

        manifest.entries.push_back(std::move(entry));

        if (progress && throttle.elapsed() >= kProgressIntervalMs) {
            throttle.restart();
            ProgressUpdate update;
            update.filesDone = filesDone;
            update.filesTotal = scan.fileCount;
            update.bytesDone = bytesDone;
            update.bytesTotal = scan.totalBytes;
            update.bytesStored = writer->storedBytes();
            update.currentItem = item.absolutePath;
            update.stage = QCoreApplication::translate("Export", "Compressing");
            progress(update);
        }
    }

    if (const auto status = packer.flush(); !status) {
        return fail(describeError(status.error()));
    }
    if (const auto status = pipeline.drain(); !status) {
        return fail(describeError(status.error()));
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

    if (const auto status = writer->finish(manifest); !status) {
        return fail(describeError(status.error()));
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
    report.elapsedMilliseconds = timer.elapsed();

    for (const auto& part : writer->parts()) {
        report.archiveParts.push_back(fromUtf8(format::fromFsPath(part)));
    }

    qCInfo(logCapture) << "captured" << report.fileCount << "files:" << formatBytes(report.rawBytes)
                       << "->" << formatBytes(report.storedBytes) << "in"
                       << report.elapsedMilliseconds << "ms";
    return report;
}

}  // namespace transmit::core
