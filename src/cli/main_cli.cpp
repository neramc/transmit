/// Headless front end for Transmit.
///
/// It exists for three reasons: automation, servers with no display, and
/// continuous integration - the cross-OS translation is exercised here with
/// `--emulate-os`, which is how the Windows and macOS paths get tested from a
/// Linux build machine.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <iostream>

#include "core/continuity/ContinuityTypes.h"
#include "core/services/ExportService.h"
#include "core/services/ImportService.h"
#include "core/services/ProfileService.h"
#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "platform/PlatformService.h"

namespace {

using namespace transmit;
using core::CancelToken;
using core::fromUtf8;
using core::toUtf8;

QTextStream& out() {
    static QTextStream stream(stdout);
    return stream;
}

QTextStream& err() {
    static QTextStream stream(stderr);
    return stream;
}

int reportError(const QString& message) {
    err() << QStringLiteral("error: ") << message << Qt::endl;
    return 1;
}

/// Reads a passphrase from a file so it never appears in the process list or
/// the shell history.
QString readPassphrase(const QCommandLineParser& parser, const QCommandLineOption& fileOption,
                       const QCommandLineOption& valueOption) {
    if (parser.isSet(fileOption)) {
        QFile file(parser.value(fileOption));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            err() << QStringLiteral("error: could not read the passphrase file") << Qt::endl;
            return {};
        }
        return QString::fromUtf8(file.readAll()).trimmed();
    }
    return parser.value(valueOption);
}

void printProgress(const core::ProgressUpdate& update) {
    static qint64 lastLength = 0;
    QString line = update.stage;
    if (update.filesTotal > 0) {
        line += QStringLiteral(" %1/%2").arg(update.filesDone).arg(update.filesTotal);
    }
    if (update.bytesTotal > 0) {
        line += QStringLiteral(" (%1 of %2)")
                    .arg(core::formatBytes(update.bytesDone), core::formatBytes(update.bytesTotal));
    }
    out() << '\r' << line.leftJustified(lastLength, u' ');
    lastLength = line.size();
    out().flush();
}

void printNotes(const QList<core::ContinuityNote>& notes) {
    if (notes.isEmpty()) {
        return;
    }
    out() << Qt::endl << QStringLiteral("Notes:") << Qt::endl;
    for (const core::ContinuityNote& note : notes) {
        out()
            << QStringLiteral("  [%1] %2").arg(core::continuityGradeName(note.grade), note.subject)
            << Qt::endl;
        out() << QStringLiteral("      ") << note.detail << Qt::endl;
    }
}

int runExport(QCommandLineParser& parser, const QCommandLineOption& outputOption,
              const QCommandLineOption& profileOption, const QCommandLineOption& presetOption,
              const QCommandLineOption& splitOption, const QCommandLineOption& passphraseOption,
              const QCommandLineOption& passphraseFileOption,
              const QCommandLineOption& domainsOption, const QCommandLineOption& labelOption) {
    if (!parser.isSet(outputOption)) {
        return reportError(QStringLiteral("--out is required"));
    }

    auto platformService = platform::PlatformService::create();
    core::ExportService service(*platformService);

    core::ExportRequest request;
    request.destinationPath = parser.value(outputOption);
    request.label = parser.value(labelOption);

    const core::CaptureProfile profile =
        core::ProfileService::profileById(parser.value(profileOption));
    request.selection = profile.selection;

    if (parser.isSet(domainsOption)) {
        request.selection.domains.clear();
        for (const QString& name : parser.value(domainsOption).split(u',', Qt::SkipEmptyParts)) {
            const auto domain = format::domainFromName(toUtf8(name.trimmed()));
            if (!domain) {
                return reportError(core::describeError(domain.error()));
            }
            request.selection.domains.insert(static_cast<int>(*domain));
        }
    }

    const auto preset = format::presetFromName(toUtf8(parser.value(presetOption)));
    if (!preset) {
        return reportError(core::describeError(preset.error()));
    }
    request.preset = *preset;

    if (parser.isSet(splitOption)) {
        const QString text = parser.value(splitOption).trimmed().toUpper();
        quint64 multiplier = 1;
        QString digits = text;
        if (text.endsWith(u'K')) {
            multiplier = 1024ULL;
            digits.chop(1);
        } else if (text.endsWith(u'M')) {
            multiplier = 1024ULL * 1024;
            digits.chop(1);
        } else if (text.endsWith(u'G')) {
            multiplier = 1024ULL * 1024 * 1024;
            digits.chop(1);
        }

        bool valid = false;
        const quint64 value = digits.toULongLong(&valid);
        if (!valid) {
            return reportError(QStringLiteral("could not read the split size '%1'")
                                   .arg(parser.value(splitOption)));
        }
        request.partSize = value * multiplier;
    }

    request.passphrase = readPassphrase(parser, passphraseFileOption, passphraseOption);
    if (request.selection.domains.contains(static_cast<int>(format::DomainId::Secrets)) &&
        request.passphrase.isEmpty()) {
        return reportError(
            QStringLiteral("capturing credentials requires a passphrase; pass --passphrase-file"));
    }

    CancelToken cancelToken;
    const core::ExportReport report = service.run(request, cancelToken, printProgress);
    out() << Qt::endl;

    if (!report.succeeded) {
        return reportError(report.errorMessage);
    }

    out() << QStringLiteral("Captured %1 files, %2 folders and %3 links")
                 .arg(report.fileCount)
                 .arg(report.directoryCount)
                 .arg(report.symlinkCount)
          << Qt::endl;
    out() << QStringLiteral("  %1 read, %2 written (%3%)")
                 .arg(core::formatBytes(report.rawBytes), core::formatBytes(report.storedBytes))
                 .arg(report.compressionRatio() * 100.0, 0, 'f', 1)
          << Qt::endl;
    if (report.deduplicatedBytes > 0) {
        out() << QStringLiteral("  %1 saved by storing repeated content once")
                     .arg(core::formatBytes(report.deduplicatedBytes))
              << Qt::endl;
    }
    out() << QStringLiteral("  encrypted: %1")
                 .arg(report.encrypted ? QStringLiteral("yes") : QStringLiteral("no"))
          << Qt::endl;
    for (const QString& part : report.archiveParts) {
        out() << QStringLiteral("  wrote ") << part << Qt::endl;
    }
    printNotes(report.notes);
    return 0;
}

int runInspect(const QString& archivePath, const QString& passphrase) {
    auto platformService = platform::PlatformService::create();
    core::ImportService service(*platformService);

    const core::ArchiveSummary summary = service.inspect(archivePath, passphrase);
    if (!summary.valid) {
        return reportError(summary.errorMessage);
    }

    out() << QStringLiteral("Archive:   %1").arg(summary.archiveId) << Qt::endl;
    if (!summary.label.isEmpty()) {
        out() << QStringLiteral("Label:     %1").arg(summary.label) << Qt::endl;
    }
    out() << QStringLiteral("Captured:  %1").arg(summary.capturedAt.toString(Qt::ISODate))
          << Qt::endl;
    out() << QStringLiteral("Parts:     %1").arg(summary.partCount) << Qt::endl;
    out() << QStringLiteral("Encrypted: %1")
                 .arg(summary.encrypted ? QStringLiteral("yes") : QStringLiteral("no"))
          << Qt::endl;

    if (!summary.unlocked) {
        out() << Qt::endl
              << QStringLiteral(
                     "This archive is locked. Pass --passphrase-file to see what is "
                     "inside it.")
              << Qt::endl;
        return 0;
    }

    out() << QStringLiteral("From:      %1 on %2 (user %3)")
                 .arg(summary.sourceOsName, summary.sourceHost, summary.sourceUser)
          << Qt::endl;
    out() << QStringLiteral("Contents:  %1 files, %2")
                 .arg(summary.fileCount)
                 .arg(core::formatBytes(summary.rawBytes))
          << Qt::endl;

    for (const format::DomainId domain : format::allDomains()) {
        const int key = static_cast<int>(domain);
        if (summary.filesPerDomain.value(key) == 0) {
            continue;
        }
        out() << QStringLiteral("  %1: %2 items, %3")
                     .arg(fromUtf8(format::domainName(domain)))
                     .arg(summary.filesPerDomain.value(key))
                     .arg(core::formatBytes(summary.bytesPerDomain.value(key)))
              << Qt::endl;
    }
    return 0;
}

int runImport(QCommandLineParser& parser, const QString& archivePath,
              const QCommandLineOption& intoOption, const QCommandLineOption& passphraseOption,
              const QCommandLineOption& passphraseFileOption,
              const QCommandLineOption& emulateOption, const QCommandLineOption& dryRunOption,
              const QCommandLineOption& conflictOption, const QCommandLineOption& domainsOption,
              const QCommandLineOption& verifyOption) {
    auto platformService = platform::PlatformService::create();
    core::ImportService service(*platformService);

    core::ImportRequest request;
    request.archivePath = archivePath;
    request.destinationOverride = parser.value(intoOption);
    request.passphrase = readPassphrase(parser, passphraseFileOption, passphraseOption);
    request.dryRun = parser.isSet(dryRunOption);
    request.verifyFirst = parser.isSet(verifyOption);

    if (parser.isSet(emulateOption)) {
        const auto family = format::osFamilyFromName(toUtf8(parser.value(emulateOption)));
        if (!family) {
            return reportError(core::describeError(family.error()));
        }
        request.emulateOs = *family;
    }

    if (parser.isSet(domainsOption)) {
        for (const QString& name : parser.value(domainsOption).split(u',', Qt::SkipEmptyParts)) {
            const auto domain = format::domainFromName(toUtf8(name.trimmed()));
            if (!domain) {
                return reportError(core::describeError(domain.error()));
            }
            request.domains.insert(static_cast<int>(*domain));
        }
    }

    const QString conflict = parser.value(conflictOption);
    if (conflict == QLatin1String("skip")) {
        request.conflictPolicy = core::ConflictPolicy::Skip;
    } else if (conflict == QLatin1String("overwrite")) {
        request.conflictPolicy = core::ConflictPolicy::Overwrite;
    } else if (conflict == QLatin1String("newer")) {
        request.conflictPolicy = core::ConflictPolicy::NewerWins;
    } else if (conflict == QLatin1String("keep-both")) {
        request.conflictPolicy = core::ConflictPolicy::KeepBoth;
    } else {
        return reportError(
            QStringLiteral("unknown conflict policy '%1' (expected skip, overwrite, newer or "
                           "keep-both)")
                .arg(conflict));
    }

    CancelToken cancelToken;
    const core::ImportReport report = service.run(request, cancelToken, printProgress);
    out() << Qt::endl;

    if (!report.succeeded) {
        return reportError(report.errorMessage);
    }

    out() << (request.dryRun ? QStringLiteral("Would restore %1 items, skipping %2")
                             : QStringLiteral("Restored %1 items, skipped %2"))
                 .arg(report.filesRestored)
                 .arg(report.filesSkipped)
          << Qt::endl;
    if (!request.dryRun) {
        out() << QStringLiteral("  %1 written").arg(core::formatBytes(report.bytesWritten))
              << Qt::endl;
    }

    if (!report.installScriptPath.isEmpty()) {
        out() << QStringLiteral("  wrote %1 to reinstall %2 program(s) - read it before running it")
                     .arg(report.installScriptPath)
                     .arg(report.programsToInstall)
              << Qt::endl;
    }

    if (!report.renames.isEmpty()) {
        out() << Qt::endl << QStringLiteral("Renamed for this system:") << Qt::endl;
        for (const auto& [from, to] : report.renames) {
            out() << QStringLiteral("  %1 -> %2").arg(from, to) << Qt::endl;
        }
    }
    printNotes(report.notes);
    return 0;
}

int runVerify(const QString& archivePath, const QString& passphrase) {
    auto readerResult = format::ArchiveReader::open(format::toFsPath(toUtf8(archivePath)));
    if (!readerResult) {
        return reportError(core::describeError(readerResult.error()));
    }
    auto reader = std::move(readerResult).value();

    if (reader->isEncrypted()) {
        if (passphrase.isEmpty()) {
            return reportError(QStringLiteral("this archive is encrypted; pass --passphrase-file"));
        }
        if (const auto status = reader->unlock(toUtf8(passphrase)); !status) {
            return reportError(core::describeError(status.error()));
        }
    }

    const auto status = reader->verifyAllBlocks([](std::size_t done, std::size_t total) {
        out() << QStringLiteral("\rchecking block %1 of %2").arg(done).arg(total);
        out().flush();
        return true;
    });
    out() << Qt::endl;

    if (!status) {
        return reportError(core::describeError(status.error()));
    }
    out() << QStringLiteral("Every block matches its recorded hash.") << Qt::endl;
    return 0;
}

int runProfiles() {
    for (const core::CaptureProfile& profile : core::ProfileService::builtInProfiles()) {
        out() << QStringLiteral("%1  %2").arg(profile.id, -12).arg(profile.displayName) << Qt::endl;
        out() << QStringLiteral("              %1").arg(profile.description) << Qt::endl
              << Qt::endl;
    }
    return 0;
}

int runDrives() {
    auto platformService = platform::PlatformService::create();
    const auto volumes = platformService->storageVolumes();

    out() << QStringLiteral("%1 %2 %3 %4 %5")
                 .arg(QStringLiteral("PATH"), -28)
                 .arg(QStringLiteral("FILESYSTEM"), -12)
                 .arg(QStringLiteral("FREE"), -12)
                 .arg(QStringLiteral("TOTAL"), -12)
                 .arg(QStringLiteral("KIND"))
          << Qt::endl;

    for (const platform::StorageVolume& volume : volumes) {
        QString kind = volume.removable ? QStringLiteral("removable") : QStringLiteral("fixed");
        if (volume.readOnly) {
            kind += QStringLiteral(", read-only");
        }
        if (volume.requiresSplitting()) {
            kind += QStringLiteral(", needs splitting");
        }
        out() << QStringLiteral("%1 %2 %3 %4 %5")
                     .arg(volume.rootPath, -28)
                     .arg(volume.fileSystem, -12)
                     .arg(core::formatBytes(volume.freeBytes), -12)
                     .arg(core::formatBytes(volume.totalBytes), -12)
                     .arg(kind)
              << Qt::endl;
    }
    return 0;
}

int runEnvironment() {
    auto platformService = platform::PlatformService::create();
    const platform::EnvironmentInfo info = platformService->environment();

    out() << QStringLiteral("System:    %1 (%2)").arg(info.osName, info.osVersion) << Qt::endl;
    out() << QStringLiteral("Family:    %1").arg(fromUtf8(format::osFamilyName(info.os)))
          << Qt::endl;
    if (!info.distroId.isEmpty()) {
        out() << QStringLiteral("Distro:    %1 (like: %2)")
                     .arg(info.distroId,
                          info.distroLike.isEmpty() ? QStringLiteral("-") : info.distroLike)
              << Qt::endl;
    }
    if (!info.desktopEnvironment.isEmpty()) {
        out() << QStringLiteral("Desktop:   %1").arg(info.desktopEnvironment) << Qt::endl;
    }
    out() << QStringLiteral("Host:      %1").arg(info.hostName) << Qt::endl;
    out() << QStringLiteral("User:      %1").arg(info.userName) << Qt::endl;
    out() << QStringLiteral("Home:      %1").arg(info.homeDirectory) << Qt::endl;
    out() << QStringLiteral("Packages:  %1")
                 .arg(platform::packageSourceName(platformService->nativePackageSource()))
          << Qt::endl;

    out() << Qt::endl << QStringLiteral("Known folders:") << Qt::endl;
    const format::PathTokenMap folders = platformService->knownFolders();
    for (const format::PathTokenId token : format::allTokens()) {
        if (const auto base = folders.base(token)) {
            out() << QStringLiteral("  {%1} %2")
                         .arg(fromUtf8(format::tokenName(token)), -12)
                         .arg(fromUtf8(*base))
                  << Qt::endl;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("transmit-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral(TRANSMIT_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("Transmit"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Move a computer's environment - files, application data, settings and the list of "
        "installed programs - onto removable media and back onto a machine running a different "
        "operating system."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("command"),
        QStringLiteral("export, import, inspect, verify, profiles, drives or environment"));
    parser.addPositionalArgument(QStringLiteral("archive"),
                                 QStringLiteral("archive path, for import, inspect and verify"));

    const QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("out")},
                                          QStringLiteral("Archive to write."),
                                          QStringLiteral("path"));
    const QCommandLineOption profileOption(QStringLiteral("profile"),
                                           QStringLiteral("Capture profile (see `profiles`)."),
                                           QStringLiteral("id"), QStringLiteral("full"));
    const QCommandLineOption presetOption(
        QStringLiteral("preset"),
        QStringLiteral("Compression: fast, balanced, maximum or extreme."), QStringLiteral("name"),
        QStringLiteral("maximum"));
    const QCommandLineOption splitOption(
        QStringLiteral("split"),
        QStringLiteral("Split into parts of this size, for example 3584M for a FAT32 stick."),
        QStringLiteral("size"));
    const QCommandLineOption passphraseOption(
        QStringLiteral("passphrase"),
        QStringLiteral("Encryption passphrase. Prefer --passphrase-file."), QStringLiteral("text"));
    const QCommandLineOption passphraseFileOption(
        QStringLiteral("passphrase-file"), QStringLiteral("Read the passphrase from this file."),
        QStringLiteral("path"));
    const QCommandLineOption domainsOption(
        QStringLiteral("domains"),
        QStringLiteral("Comma-separated: userdata, appstate, settings, secrets, apps."),
        QStringLiteral("list"));
    const QCommandLineOption labelOption(QStringLiteral("label"),
                                         QStringLiteral("Free-text label for the archive."),
                                         QStringLiteral("text"));
    const QCommandLineOption intoOption(
        QStringLiteral("into"),
        QStringLiteral("Restore into this folder instead of your real folders."),
        QStringLiteral("path"));
    const QCommandLineOption emulateOption(
        QStringLiteral("emulate-os"),
        QStringLiteral("Translate as if restoring onto windows, macos or linux."),
        QStringLiteral("name"));
    const QCommandLineOption dryRunOption(
        QStringLiteral("dry-run"), QStringLiteral("Report what would happen without writing."));
    const QCommandLineOption conflictOption(
        QStringLiteral("conflict"),
        QStringLiteral("When a file already exists: skip, overwrite, newer or keep-both."),
        QStringLiteral("policy"), QStringLiteral("keep-both"));
    const QCommandLineOption verifyOption(QStringLiteral("verify"),
                                          QStringLiteral("Check every block before restoring."));
    const QCommandLineOption verboseOption(QStringLiteral("verbose"),
                                           QStringLiteral("Log what is happening in detail."));

    parser.addOptions({outputOption, profileOption, presetOption, splitOption, passphraseOption,
                       passphraseFileOption, domainsOption, labelOption, intoOption, emulateOption,
                       dryRunOption, conflictOption, verifyOption, verboseOption});
    parser.process(app);

    core::configureLogging(parser.isSet(verboseOption));

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        parser.showHelp(1);
    }

    const QString command = positional.first();
    const QString archive = positional.size() > 1 ? positional.at(1) : QString();
    const QString passphrase = readPassphrase(parser, passphraseFileOption, passphraseOption);

    if (command == QLatin1String("export")) {
        return runExport(parser, outputOption, profileOption, presetOption, splitOption,
                         passphraseOption, passphraseFileOption, domainsOption, labelOption);
    }
    if (command == QLatin1String("import")) {
        if (archive.isEmpty()) {
            return reportError(QStringLiteral("import needs an archive path"));
        }
        return runImport(parser, archive, intoOption, passphraseOption, passphraseFileOption,
                         emulateOption, dryRunOption, conflictOption, domainsOption, verifyOption);
    }
    if (command == QLatin1String("inspect")) {
        if (archive.isEmpty()) {
            return reportError(QStringLiteral("inspect needs an archive path"));
        }
        return runInspect(archive, passphrase);
    }
    if (command == QLatin1String("verify")) {
        if (archive.isEmpty()) {
            return reportError(QStringLiteral("verify needs an archive path"));
        }
        return runVerify(archive, passphrase);
    }
    if (command == QLatin1String("profiles")) {
        return runProfiles();
    }
    if (command == QLatin1String("drives")) {
        return runDrives();
    }
    if (command == QLatin1String("environment")) {
        return runEnvironment();
    }

    return reportError(QStringLiteral("unknown command '%1'").arg(command));
}
