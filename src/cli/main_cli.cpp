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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <iostream>

#include "core/continuity/ContinuityTypes.h"
#include "core/continuity/SelectionCodec.h"
#include "core/recipe/RecipeCatalog.h"
#include "core/services/ExportService.h"
#include "core/services/ImportService.h"
#include "core/services/ProfileService.h"
#include "core/services/RepairService.h"
#include "core/services/RollbackWriter.h"
#include "core/services/ScanService.h"
#include "core/services/VerifyService.h"
#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/FileIo.h"
#include "platform/PlatformService.h"

#include "cli/Terminal.h"

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

/// The passphrase to use, from whichever source the run actually has.
///
/// `needed` says the command cannot go ahead without one - credentials are
/// being captured, or the archive is locked - and is what turns a terminal
/// into a prompt. Without a terminal nothing is asked: a script piping input
/// would hang on a question nobody can see, so it gets the empty string and
/// the caller's own error message instead.
QString resolvePassphrase(const QCommandLineParser& parser, const QCommandLineOption& fileOption,
                          const QCommandLineOption& valueOption,
                          const QCommandLineOption& askOption, bool needed, bool confirm) {
    if (parser.isSet(fileOption) || parser.isSet(valueOption)) {
        return readPassphrase(parser, fileOption, valueOption);
    }
    if (!parser.isSet(askOption) && !needed) {
        return {};
    }
    return cli::askForPassphrase(QStringLiteral("Passphrase"), confirm).value_or(QString());
}

/// Whether the archive at this path is locked.
///
/// Opening reads the header rather than the contents, so this costs about as
/// much as a stat - cheap enough to ask before deciding whether to prompt.
bool archiveIsEncrypted(const QString& archivePath) {
    auto reader = format::ArchiveReader::open(format::toFsPath(toUtf8(archivePath)));
    return reader && (*reader)->isEncrypted();
}

/// The shell's convention for "killed by SIGINT", which is what a Ctrl-C at
/// the prompt should look like to whatever ran us.
constexpr int kInterruptedExitCode = 130;

/// Some of it worked and some did not. Distinct from 1, which means the
/// whole command failed and the machine was not changed.
constexpr int kPartialExitCode = 2;

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

/// "512", "64K", "3584M", "2G". Returns nothing when it is not a size.
std::optional<quint64> parseSize(const QString& text) {
    const QString upper = text.trimmed().toUpper();
    if (upper.isEmpty()) {
        return std::nullopt;
    }

    quint64 multiplier = 1;
    QString digits = upper;
    if (upper.endsWith(u'K')) {
        multiplier = 1024ULL;
        digits.chop(1);
    } else if (upper.endsWith(u'M')) {
        multiplier = 1024ULL * 1024;
        digits.chop(1);
    } else if (upper.endsWith(u'G')) {
        multiplier = 1024ULL * 1024 * 1024;
        digits.chop(1);
    }

    bool valid = false;
    const quint64 value = digits.toULongLong(&valid);
    if (!valid) {
        return std::nullopt;
    }
    return value * multiplier;
}

/// "30d", "6m", "2y", or an ISO date. Relative forms because that is how
/// people think about it - "anything I have touched this year" - and the
/// absolute one because a script wants a fixed boundary.
std::optional<QDateTime> parseWhen(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    const QChar unit = trimmed.back().toLower();
    if (unit == u'd' || unit == u'w' || unit == u'm' || unit == u'y') {
        bool valid = false;
        const int count = QStringView(trimmed).chopped(1).toInt(&valid);
        if (!valid || count < 0) {
            return std::nullopt;
        }
        const QDateTime now = QDateTime::currentDateTime();
        switch (unit.unicode()) {
            case u'd':
                return now.addDays(-count);
            case u'w':
                return now.addDays(-count * 7);
            case u'm':
                return now.addMonths(-count);
            default:
                return now.addYears(-count);
        }
    }

    const QDateTime absolute = QDateTime::fromString(trimmed, Qt::ISODate);
    return absolute.isValid() ? std::optional<QDateTime>(absolute) : std::nullopt;
}

/// The extensions in a comma-separated list, lowercase and without dots, so
/// "--include-ext .TXT, md" means what it looks like it means.
QSet<QString> parseExtensions(const QString& text) {
    QSet<QString> extensions;
    for (const QString& piece : text.split(u',', Qt::SkipEmptyParts)) {
        QString extension = piece.trimmed().toLower();
        while (extension.startsWith(u'.')) {
            extension.remove(0, 1);
        }
        if (!extension.isEmpty()) {
            extensions.insert(extension);
        }
    }
    return extensions;
}

/// Where the time went, longest first, with what each stage is a share of.
///
/// The share is the point. A stage that took four seconds means nothing on
/// its own; four seconds out of six says where to look, and one line saying
/// what was not measured stops the columns from having to add up to something
/// they were never going to add up to.
void printTimings(const QList<core::StageTiming>& stages, qint64 totalMilliseconds) {
    if (stages.isEmpty()) {
        return;
    }

    QList<core::StageTiming> sorted = stages;
    std::sort(sorted.begin(), sorted.end(),
              [](const core::StageTiming& a, const core::StageTiming& b) {
                  return a.nanoseconds > b.nanoseconds;
              });

    const auto total = static_cast<double>(totalMilliseconds);
    double measured = 0;

    out() << Qt::endl << QStringLiteral("Where the time went") << Qt::endl;
    for (const core::StageTiming& stage : sorted) {
        measured += stage.milliseconds();
        out() << QStringLiteral("  %1 %2 %3  (%4)")
                     .arg(stage.name, -12)
                     .arg(stage.milliseconds(), 10, 'f', 1)
                     .arg(QStringLiteral("ms"))
                     .arg(total > 0 ? QStringLiteral("%1%").arg(
                                          stage.milliseconds() / total * 100.0, 0, 'f', 1)
                                    : QStringLiteral("-"))
              << Qt::endl;
    }
    out() << QStringLiteral("  %1 %2 ms").arg(QStringLiteral("total"), -12).arg(total, 10, 'f', 1)
          << Qt::endl;
    if (total > measured) {
        out() << QStringLiteral("  %1 %2 ms  (everything between the stages above)")
                     .arg(QStringLiteral("unmeasured"), -12)
                     .arg(total - measured, 10, 'f', 1)
              << Qt::endl;
    }
}

/// What a capture would do, without doing any of it.
///
/// Built from the same request `export` builds from the same options, so the
/// two cannot drift: a dry run that described a different capture from the one
/// that follows is worse than no dry run.
int printPlan(const platform::PlatformService& platform, const core::ExportService& service,
              const core::ExportRequest& request) {
    CancelToken cancelToken;
    const cli::InterruptHandler interrupts(cancelToken);

    const core::ScanService scanner(platform);
    const core::ScanResult scan = scanner.scan(request.selection, cancelToken, {});
    if (cli::InterruptHandler::wasInterrupted()) {
        return kInterruptedExitCode;
    }

    out() << Qt::endl << QStringLiteral("What would be captured") << Qt::endl;
    out() << QStringLiteral("  %1 files, %2 folders, %3 links")
                 .arg(scan.fileCount)
                 .arg(scan.directoryCount)
                 .arg(scan.symlinkCount)
          << Qt::endl;
    out() << QStringLiteral("  %1 before compression").arg(core::formatBytes(scan.totalBytes))
          << Qt::endl;

    if (scan.skippedCount > 0) {
        out() << QStringLiteral("  %1 left out:").arg(scan.skippedCount) << Qt::endl;
        for (auto it = scan.skippedByReason.constBegin(); it != scan.skippedByReason.constEnd();
             ++it) {
            out() << QStringLiteral("      %1 %2")
                         .arg(it.value(), 8)
                         .arg(core::skipReasonName(static_cast<core::SkipReason>(it.key())))
                  << Qt::endl;
        }
    }
    if (scan.incomplete()) {
        out() << QStringLiteral("  %1 folder(s) could not be opened at all:")
                     .arg(scan.unreadableDirectories.size())
              << Qt::endl;
        for (const QString& path : scan.unreadableDirectories) {
            out() << QStringLiteral("      ") << path << Qt::endl;
        }
    }

    // ------------------------------------------------------- the drive
    const platform::StorageVolume drive = service.volumeForPath(request.destinationPath);
    out() << Qt::endl << QStringLiteral("Where it would go") << Qt::endl;
    out() << QStringLiteral("  %1").arg(QDir::toNativeSeparators(request.destinationPath))
          << Qt::endl;
    if (drive.totalBytes > 0) {
        out() << QStringLiteral("  %1 (%2), %3 free of %4%5")
                     .arg(drive.displayName.isEmpty() ? drive.rootPath : drive.displayName,
                          drive.fileSystem, core::formatBytes(drive.freeBytes),
                          core::formatBytes(drive.totalBytes),
                          drive.removable ? QStringLiteral(", removable") : QString())
              << Qt::endl;
        if (drive.readOnly) {
            out() << QStringLiteral("  it is write-protected, so this capture would be refused")
                  << Qt::endl;
        } else if (drive.freeBytes < scan.totalBytes / 20) {
            out() << QStringLiteral("  too small: this capture would be refused") << Qt::endl;
        } else if (drive.freeBytes < scan.totalBytes) {
            out() << QStringLiteral(
                         "  less free than there is to copy; it may still fit once "
                         "compressed")
                  << Qt::endl;
        }
        if (drive.requiresSplitting()) {
            out() << QStringLiteral(
                         "  this filesystem cannot hold a file of 4 GB or more, so the "
                         "archive would be written in numbered parts")
                  << Qt::endl;
        }
    } else {
        out() << QStringLiteral(
                     "  this drive is not one the system enumerates, so there is "
                     "nothing to say about its free space")
              << Qt::endl;
    }

    // ------------------------------------------------------- the steps
    out() << Qt::endl << QStringLiteral("What would happen, in order") << Qt::endl;
    const QStringList steps = {
        QStringLiteral("check the drive: space, write protection, whether it needs splitting"),
        QStringLiteral("ask which programs are running and holding their data open"),
        QStringLiteral("take a filesystem snapshot where this system can"),
        QStringLiteral("read, hash and pack every file"),
        QStringLiteral("write the parts to the drive"),
        QStringLiteral("finish: manifest, footer, part headers, and push it all to the device"),
        request.packaging.writeMd5Sidecar ? QStringLiteral("write the .md5 file beside it")
                                          : QStringLiteral("no .md5 file (--no-md5-sidecar)"),
        request.packaging.verifyAfterWriting
            ? QStringLiteral("read the whole archive back off the drive and check every file")
            : QStringLiteral("no read-back (--no-verify-after)"),
    };
    int number = 1;
    for (const QString& step : steps) {
        out() << QStringLiteral("  %1. %2").arg(number++).arg(step) << Qt::endl;
    }

    out() << Qt::endl << QStringLiteral("How it would be packed") << Qt::endl;
    out() << QStringLiteral("  compression: %1")
                 .arg(QString::fromUtf8(format::presetName(request.packaging.preset)))
          << Qt::endl;
    out() << QStringLiteral("  encrypted:   %1")
                 .arg(request.passphrase.isEmpty() ? QStringLiteral("no") : QStringLiteral("yes"))
          << Qt::endl;
    out() << QStringLiteral("  per-file MD5: %1")
                 .arg(request.packaging.recordMd5 ? QStringLiteral("yes") : QStringLiteral("no"))
          << Qt::endl;

    out() << Qt::endl << QStringLiteral("Nothing was written.") << Qt::endl;
    printNotes(scan.notes);
    return 0;
}

int runExport(QCommandLineParser& parser, const QCommandLineOption& outputOption,
              const QCommandLineOption& profileOption, const QCommandLineOption& presetOption,
              const QCommandLineOption& splitOption, const QCommandLineOption& passphraseOption,
              const QCommandLineOption& passphraseFileOption,
              const QCommandLineOption& askPassphraseOption,
              const QCommandLineOption& domainsOption, const QCommandLineOption& labelOption,
              bool planOnly = false) {
    if (!parser.isSet(outputOption)) {
        return reportError(QStringLiteral("--out is required"));
    }

    auto platformService = platform::PlatformService::create();
    core::ExportService service(*platformService);

    core::ExportRequest request;
    request.destinationPath = parser.value(outputOption);

    // A saved selection is the starting point; anything typed on the command
    // line goes on top of it. That way one file can hold the settled choices
    // and a flag can vary the one thing that differs this time.
    core::CaptureDocument document;
    if (parser.isSet(QStringLiteral("selection-file"))) {
        const QString path = parser.value(QStringLiteral("selection-file"));
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return reportError(
                QStringLiteral("could not read %1: %2").arg(path, file.errorString()));
        }
        QString error;
        if (!core::SelectionCodec::decode(file.readAll(), document, &error)) {
            return reportError(QStringLiteral("%1: %2").arg(path, error));
        }
    } else {
        const core::CaptureProfile profile =
            core::ProfileService::profileById(parser.value(profileOption));
        document.selection = profile.selection;
    }

    request.selection = document.selection;
    request.packaging = document.packaging;
    request.label = parser.isSet(labelOption) ? parser.value(labelOption) : document.label;

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

    if (parser.isSet(presetOption) || !parser.isSet(QStringLiteral("selection-file"))) {
        const auto preset = format::presetFromName(toUtf8(parser.value(presetOption)));
        if (!preset) {
            return reportError(core::describeError(preset.error()));
        }
        request.packaging.preset = *preset;
    }

    if (parser.isSet(splitOption)) {
        const auto size = parseSize(parser.value(splitOption));
        if (!size) {
            return reportError(QStringLiteral("could not read the split size '%1'")
                                   .arg(parser.value(splitOption)));
        }
        request.packaging.partSize = *size;
    }

    // ------------------------------------------------------- what to take
    core::ScopeRule& scope = request.selection.scope;

    const auto readSize = [&parser](const char* name, quint64& into) -> QString {
        const QString option = QString::fromLatin1(name);
        if (!parser.isSet(option)) {
            return {};
        }
        const auto size = parseSize(parser.value(option));
        if (!size) {
            return QStringLiteral("could not read --%1 '%2'").arg(option, parser.value(option));
        }
        into = *size;
        return {};
    };

    for (const auto& [name, target] : std::initializer_list<std::pair<const char*, quint64*>>{
             {"max-file-size", &scope.maximumFileSize},
             {"min-file-size", &scope.minimumFileSize},
             {"block-size", &request.packaging.solidBlockSize},
             {"sync-every", &request.packaging.syncIntervalBytes}}) {
        if (const QString problem = readSize(name, *target); !problem.isEmpty()) {
            return reportError(problem);
        }
    }

    for (const auto& [name, target] : std::initializer_list<std::pair<const char*, QDateTime*>>{
             {"modified-since", &scope.modifiedSince},
             {"modified-before", &scope.modifiedBefore}}) {
        const QString option = QString::fromLatin1(name);
        if (!parser.isSet(option)) {
            continue;
        }
        const auto when = parseWhen(parser.value(option));
        if (!when) {
            return reportError(QStringLiteral("could not read --%1 '%2'; try 30d, 6m, 2y or a "
                                              "date like 2025-01-31")
                                   .arg(option, parser.value(option)));
        }
        *target = *when;
    }

    if (parser.isSet(QStringLiteral("include-ext"))) {
        scope.includeExtensions = parseExtensions(parser.value(QStringLiteral("include-ext")));
    }
    if (parser.isSet(QStringLiteral("exclude-ext"))) {
        scope.excludeExtensions = parseExtensions(parser.value(QStringLiteral("exclude-ext")));
    }
    scope.excludePatterns += parser.values(QStringLiteral("exclude"));
    if (parser.isSet(QStringLiteral("no-hidden"))) {
        scope.includeHidden = false;
    }
    if (parser.isSet(QStringLiteral("follow-symlinks"))) {
        scope.followSymlinks = true;
    }

    if (parser.isSet(QStringLiteral("workers"))) {
        bool valid = false;
        const int workers = parser.value(QStringLiteral("workers")).toInt(&valid);
        if (!valid || workers < 0) {
            return reportError(QStringLiteral("--workers needs a count of 0 or more"));
        }
        request.packaging.workerCount = workers;
    }
    if (parser.isSet(QStringLiteral("no-verify-after"))) {
        request.packaging.verifyAfterWriting = false;
    }
    if (parser.isSet(QStringLiteral("no-md5"))) {
        request.packaging.recordMd5 = false;
        request.packaging.writeMd5Sidecar = false;
    }
    if (parser.isSet(QStringLiteral("no-md5-sidecar"))) {
        request.packaging.writeMd5Sidecar = false;
    }
    if (parser.isSet(QStringLiteral("md5-sidecar-names"))) {
        request.packaging.sidecarNamesEvenWhenEncrypted = true;
    }

    // ------------------------------------------------ which applications
    if (parser.isSet(QStringLiteral("apps"))) {
        const QString value = parser.value(QStringLiteral("apps")).trimmed();
        if (value.compare(QLatin1String("all"), Qt::CaseInsensitive) == 0) {
            request.selection.appMode = core::AppSelectionMode::All;
        } else if (value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) {
            request.selection.appMode = core::AppSelectionMode::None;
        } else {
            request.selection.appMode = core::AppSelectionMode::Explicit;
            for (const QString& id : value.split(u',', Qt::SkipEmptyParts)) {
                core::AppSelection app;
                app.appId = id.trimmed();
                app.captureState = true;
                app.stateRootIds =
                    parser.value(QStringLiteral("app-roots")).split(u',', Qt::SkipEmptyParts);
                request.selection.apps.push_back(app);
            }
        }
    }

    for (const QString& id :
         parser.value(QStringLiteral("no-app-data")).split(u',', Qt::SkipEmptyParts)) {
        // Named after the mode on purpose: "--apps all --no-app-data
        // com.spotify.client" is the common case, and an explicit entry always
        // wins over the mode.
        core::AppSelection app;
        app.appId = id.trimmed();
        app.captureState = false;
        app.recordForReinstall = true;
        request.selection.apps.push_back(app);
    }

    if (parser.isSet(QStringLiteral("save-selection"))) {
        core::CaptureDocument saved;
        saved.selection = request.selection;
        saved.packaging = request.packaging;
        saved.label = request.label;

        const QString path = parser.value(QStringLiteral("save-selection"));
        const QByteArray encoded = core::SelectionCodec::encode(saved);
        const auto written = format::writeFileAtomically(
            format::toFsPath(path.toUtf8().toStdString()),
            format::ByteView(reinterpret_cast<const std::byte*>(encoded.constData()),
                             static_cast<std::size_t>(encoded.size())));
        if (!written) {
            return reportError(QStringLiteral("could not write %1: %2")
                                   .arg(path, core::describeError(written.error())));
        }
        out() << QStringLiteral("saved these choices to ") << path << Qt::endl;
    }

    const bool capturingSecrets =
        request.selection.domains.contains(static_cast<int>(format::DomainId::Secrets));
    if (capturingSecrets) {
        // Said before the passphrase is asked for rather than in the report
        // afterwards, so that it can still change somebody's mind.
        err() << QStringLiteral(
                     "note: saved passwords will be written into this archive. Keep the drive "
                     "and the passphrase apart.")
              << Qt::endl;
    }

    request.passphrase = resolvePassphrase(parser, passphraseFileOption, passphraseOption,
                                           askPassphraseOption, capturingSecrets, true);
    if (capturingSecrets && request.passphrase.isEmpty()) {
        return reportError(
            QStringLiteral("capturing saved passwords requires a passphrase; run this from a "
                           "terminal or pass --passphrase-file"));
    }

    // Everything above built the request the capture would run. `plan` stops
    // here and describes it, which is the only way the two can agree about
    // what would happen.
    if (planOnly) {
        return printPlan(*platformService, service, request);
    }

    CancelToken cancelToken;
    const cli::InterruptHandler interrupts(cancelToken);
    const core::ExportReport report = service.run(request, cancelToken, printProgress);
    out() << Qt::endl;

    if (!report.succeeded) {
        if (cli::InterruptHandler::wasInterrupted()) {
            err() << QStringLiteral(
                         "stopped. The part-written archive was removed: one that stops half way "
                         "cannot be restored from.")
                  << Qt::endl;
            return kInterruptedExitCode;
        }
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

    if (parser.isSet(QStringLiteral("timings"))) {
        printTimings(report.stages, report.elapsedMilliseconds);
    }

    // Said plainly and separately from the notes, because somebody who is
    // about to wipe this machine needs to know the archive is not everything
    // they asked for before they do it.
    if (report.incomplete) {
        err() << Qt::endl
              << QStringLiteral(
                     "Warning: %1 folder(s) could not be opened, so nothing inside them was "
                     "captured. This archive is not a complete copy of what you selected.")
                     .arg(report.unreadablePaths.size())
              << Qt::endl;
        for (const QString& path : report.unreadablePaths) {
            err() << QStringLiteral("  ") << path << Qt::endl;
        }
    }

    printNotes(report.notes);
    return report.incomplete ? kPartialExitCode : 0;
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
                     "This archive is locked. Run this from a terminal to be asked for the "
                     "passphrase, or pass --passphrase-file.")
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
              const QCommandLineOption& askPassphraseOption,
              const QCommandLineOption& emulateOption, const QCommandLineOption& dryRunOption,
              const QCommandLineOption& conflictOption, const QCommandLineOption& domainsOption,
              const QCommandLineOption& verifyOption) {
    auto platformService = platform::PlatformService::create();
    core::ImportService service(*platformService);

    core::ImportRequest request;
    request.archivePath = archivePath;
    request.destinationOverride = parser.value(intoOption);
    request.passphrase =
        resolvePassphrase(parser, passphraseFileOption, passphraseOption, askPassphraseOption,
                          archiveIsEncrypted(archivePath), false);
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
    const cli::InterruptHandler interrupts(cancelToken);
    const core::ImportReport report = service.run(request, cancelToken, printProgress);
    out() << Qt::endl;

    if (!report.succeeded) {
        if (cli::InterruptHandler::wasInterrupted()) {
            err() << QStringLiteral("stopped part way through.") << Qt::endl;
            if (!report.rollbackArchivePath.isEmpty()) {
                err() << QStringLiteral(
                             "What was restored so far can be undone: transmit-cli "
                             "rollback %1")
                             .arg(report.rollbackArchivePath)
                      << Qt::endl;
            }
            return kInterruptedExitCode;
        }
        // A run where some files landed and some did not is neither a
        // success nor a plain error: the machine has been changed. Say so
        // and carry on printing the summary, so the caller can see what
        // did work and where the undo point is.
        if (!report.partial()) {
            return reportError(report.errorMessage);
        }
        err() << QStringLiteral("warning: ") << report.errorMessage << Qt::endl;
    }

    out() << (request.dryRun ? QStringLiteral("Would restore %1 items, skipping %2")
                             : QStringLiteral("Restored %1 items, skipped %2"))
                 .arg(report.filesRestored)
                 .arg(report.filesSkipped)
          << Qt::endl;
    if (report.filesFailed > 0) {
        out() << QStringLiteral("  %1 could not be restored - see the notes below")
                     .arg(report.filesFailed)
              << Qt::endl;
    }
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

    // An undo point nobody is told about is an archive quietly taking up room
    // in someone's home directory.
    if (!report.rollbackArchivePath.isEmpty()) {
        out() << QStringLiteral("  this can be undone: transmit-cli rollback %1")
                     .arg(report.rollbackArchivePath)
              << Qt::endl;
    }

    if (!report.renames.isEmpty()) {
        out() << Qt::endl << QStringLiteral("Renamed for this system:") << Qt::endl;
        for (const auto& [from, to] : report.renames) {
            out() << QStringLiteral("  %1 -> %2").arg(from, to) << Qt::endl;
        }
    }
    printNotes(report.notes);

    // 0 all of it, 2 some of it, 1 none of it. A script that only checks
    // for zero should not be told a half-restored machine was fine.
    return report.filesFailed > 0 ? kPartialExitCode : 0;
}

/// The verification report as JSON, for a script that has to decide something.
QJsonObject verifyReportAsJson(const core::VerifyReport& report) {
    QJsonObject root;
    root.insert(QStringLiteral("succeeded"), report.succeeded);
    root.insert(QStringLiteral("filesChecked"), static_cast<qint64>(report.filesChecked));
    root.insert(QStringLiteral("filesFailed"), static_cast<qint64>(report.filesFailed));
    root.insert(QStringLiteral("bytesRead"), static_cast<qint64>(report.bytesRead));
    root.insert(QStringLiteral("elapsedMilliseconds"), report.elapsedMilliseconds);
    root.insert(QStringLiteral("retriedReads"), static_cast<qint64>(report.retriedReads));

    // Said out loud rather than left for a script to assume: without it the
    // read-back may have come from memory.
    root.insert(QStringLiteral("readPastTheCache"), report.cacheDropped);
    root.insert(QStringLiteral("filesFromRepair"), static_cast<qint64>(report.filesFromRepair));
    root.insert(QStringLiteral("usedRepair"), report.usedRepair);
    if (!report.errorMessage.isEmpty()) {
        root.insert(QStringLiteral("error"), report.errorMessage);
    }

    QJsonArray failures;
    for (const core::VerifyFileResult& failure : report.failures) {
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), failure.path);
        entry.insert(QStringLiteral("status"), core::verifyStatusName(failure.status));
        entry.insert(QStringLiteral("size"), static_cast<qint64>(failure.size));
        entry.insert(QStringLiteral("attempts"), failure.attempts);
        if (failure.fromRepair) {
            entry.insert(QStringLiteral("fromRepair"), true);
        }
        if (!failure.appId.isEmpty()) {
            entry.insert(QStringLiteral("appId"), failure.appId);
        }
        if (!failure.detail.isEmpty()) {
            entry.insert(QStringLiteral("detail"), failure.detail);
        }
        failures.push_back(entry);
    }
    root.insert(QStringLiteral("failures"), failures);

    QJsonArray parts;
    for (const core::VerifyPartResult& part : report.parts) {
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), part.path);
        entry.insert(QStringLiteral("size"), static_cast<qint64>(part.size));
        entry.insert(QStringLiteral("endsMatched"), part.endsMatched);
        entry.insert(QStringLiteral("checksumMatched"), part.md5Matched);
        if (!part.detail.isEmpty()) {
            entry.insert(QStringLiteral("detail"), part.detail);
        }
        parts.push_back(entry);
    }
    root.insert(QStringLiteral("parts"), parts);
    return root;
}

int runVerify(const QString& archivePath, const QString& passphrase, bool deep, bool asJson) {
    CancelToken cancelToken;
    const cli::InterruptHandler interrupts(cancelToken);

    if (!deep) {
        // The shallow check: does every block decompress and match its own
        // hash. Faster, and enough to answer "is this archive readable at
        // all"; it says nothing about whether the entry table still points at
        // the right bytes, which is what --deep is for.
        auto readerResult = format::ArchiveReader::open(format::toFsPath(toUtf8(archivePath)));
        if (!readerResult) {
            return reportError(core::describeError(readerResult.error()));
        }
        auto reader = std::move(readerResult).value();

        if (reader->isEncrypted()) {
            if (passphrase.isEmpty()) {
                return reportError(QStringLiteral(
                    "this archive is encrypted; run this from a terminal to be asked "
                    "for the passphrase, or pass --passphrase-file"));
            }
            if (const auto status = reader->unlock(toUtf8(passphrase)); !status) {
                return reportError(core::describeError(status.error()));
            }
        }

        const auto status =
            reader->verifyAllBlocks([&cancelToken, asJson](std::size_t done, std::size_t total) {
                if (!asJson) {
                    out() << QStringLiteral("\rchecking block %1 of %2").arg(done).arg(total);
                    out().flush();
                }
                return !cancelToken.isCancelled();
            });
        if (!asJson) {
            out() << Qt::endl;
        }

        if (!status) {
            if (cli::InterruptHandler::wasInterrupted()) {
                err() << QStringLiteral("stopped before every block had been checked.") << Qt::endl;
                return kInterruptedExitCode;
            }
            return reportError(core::describeError(status.error()));
        }
        if (!asJson) {
            out() << QStringLiteral("Every block matches its recorded hash.") << Qt::endl;
        }
        return 0;
    }

    const auto platform = platform::PlatformService::create();
    const core::VerifyService verifier;

    core::VerifyRequest request;
    request.archivePath = archivePath;
    request.passphrase = passphrase;
    request.deep = true;

    const core::VerifyReport report =
        verifier.run(request, cancelToken, [asJson](const core::ProgressUpdate& update) {
            if (asJson || update.filesTotal == 0) {
                return;
            }
            out() << QStringLiteral("\rchecking %1 of %2")
                         .arg(update.filesDone)
                         .arg(update.filesTotal);
            out().flush();
        });

    if (asJson) {
        out() << QString::fromUtf8(
                     QJsonDocument(verifyReportAsJson(report)).toJson(QJsonDocument::Indented))
              << Qt::flush;
    } else {
        out() << Qt::endl;
        if (report.succeeded) {
            out() << QStringLiteral("Every one of %1 file(s) came back off the drive unchanged.")
                         .arg(report.filesChecked)
                  << Qt::endl;
        } else if (!report.errorMessage.isEmpty()) {
            err() << report.errorMessage << Qt::endl;
        }
        if (!report.cacheDropped) {
            out() << QStringLiteral(
                         "  note: this system cannot be asked to forget its cached copy of a "
                         "file, so some of this may have been read from memory rather than "
                         "from the drive.")
                  << Qt::endl;
        }
        if (report.retriedReads > 0) {
            out() << QStringLiteral("  %1 read(s) only worked after retrying.")
                         .arg(report.retriedReads)
                  << Qt::endl;
        }
        if (report.filesFromRepair > 0) {
            out() << QStringLiteral(
                         "  %1 file(s) came from the repair archive beside this one. The data "
                         "is there; this archive's own copy of it is not.")
                         .arg(report.filesFromRepair)
                  << Qt::endl;
        }
        for (const core::VerifyFileResult& failure : report.failures) {
            // A file the repair supplied is in this list because something is
            // wrong with the archive, not because the file is lost. Saying
            // "matched" next to it under a heading about failures would read
            // as the opposite of what happened.
            if (failure.fromRepair) {
                out() << QStringLiteral("  %1: read from the repair archive").arg(failure.path)
                      << Qt::endl;
                continue;
            }
            err() << QStringLiteral("  %1: %2")
                         .arg(failure.path, core::verifyStatusName(failure.status))
                  << Qt::endl;
        }
    }

    if (cli::InterruptHandler::wasInterrupted()) {
        return kInterruptedExitCode;
    }
    if (report.succeeded) {
        return 0;
    }
    // Some of it came back and some did not, which is a different thing from
    // an archive that could not be read at all. A file the repair supplied
    // counts as "some of it": the archive is damaged and the data is there.
    return report.filesFailed > 0 || report.filesFromRepair > 0 ? kPartialExitCode : 1;
}

/// The paths a `verify --json` report named as damaged.
///
/// Taking the machine's own output rather than asking somebody to retype a
/// list of file names: the two runs are then talking about exactly the same
/// files, and a path with a space in it cannot be split in half on the way.
QStringList pathsFromVerifyReport(const QString& reportPath, QString* errorMessage) {
    QFile file(reportPath);
    if (!file.open(QIODevice::ReadOnly)) {
        *errorMessage = QStringLiteral("could not read '%1'").arg(reportPath);
        return {};
    }

    QJsonParseError parsed{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parsed);
    if (parsed.error != QJsonParseError::NoError) {
        *errorMessage = QStringLiteral("'%1' is not the JSON a verify run writes: %2")
                            .arg(reportPath, parsed.errorString());
        return {};
    }

    QStringList paths;
    for (const QJsonValue& failure :
         document.object().value(QStringLiteral("failures")).toArray()) {
        const QString path = failure.toObject().value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) {
            paths.push_back(path);
        }
    }
    return paths;
}

int runRepair(const QString& archivePath, const QString& passphrase, const QString& fromReport) {
    CancelToken cancelToken;
    const cli::InterruptHandler interrupts(cancelToken);

    core::RepairRequest request;
    request.archivePath = archivePath;
    request.passphrase = passphrase;

    if (!fromReport.isEmpty()) {
        QString problem;
        request.paths = pathsFromVerifyReport(fromReport, &problem);
        if (!problem.isEmpty()) {
            return reportError(problem);
        }
        if (request.paths.isEmpty()) {
            out() << QStringLiteral("That report lists no damaged files.") << Qt::endl;
            return 0;
        }
    }

    const auto platform = platform::PlatformService::create();
    const core::RepairService repairer(*platform);
    const core::RepairReport report =
        repairer.run(request, cancelToken, [](const core::ProgressUpdate& update) {
            if (update.filesTotal == 0) {
                return;
            }
            out() << QStringLiteral("\r%1 %2 of %3")
                         .arg(update.stage)
                         .arg(update.filesDone)
                         .arg(update.filesTotal);
            out().flush();
        });
    out() << Qt::endl;

    if (!report.succeeded) {
        for (const core::RepairFailure& failure : report.failures) {
            err() << QStringLiteral("  %1: %2")
                         .arg(failure.path, core::repairObstacleName(failure.obstacle))
                  << Qt::endl;
        }
        return reportError(report.errorMessage);
    }

    if (report.filesNeedingRepair == 0) {
        out() << QStringLiteral("Nothing needed repairing: the archive reads back correctly.")
              << Qt::endl;
        return 0;
    }

    out() << QStringLiteral("Recovered %1 of %2 file(s) into %3")
                 .arg(report.filesRepaired)
                 .arg(report.filesNeedingRepair)
                 .arg(report.repairPath)
          << Qt::endl;
    out() << QStringLiteral(
                 "  The original archive was not modified. Keep the two together - "
                 "reading the archive picks the repair up on its own.")
          << Qt::endl;

    for (const core::RepairFailure& failure : report.failures) {
        err() << QStringLiteral("  %1: %2")
                     .arg(failure.path, core::repairObstacleName(failure.obstacle))
              << Qt::endl;
        if (!failure.detail.isEmpty()) {
            err() << QStringLiteral("      ") << failure.detail << Qt::endl;
        }
    }

    // Some of it recovered and some of it not is its own answer, the same way
    // a partial restore is.
    return report.everythingRecovered() ? 0 : kPartialExitCode;
}

int runRollback(const QString& archivePath) {
    const auto result = core::RollbackWriter::undo(archivePath);
    if (!result) {
        return reportError(core::describeError(result.error()));
    }

    out() << QStringLiteral("Put back %1 file(s) and removed %2 that the restore had added")
                 .arg(result->filesRestored)
                 .arg(result->filesRemoved)
          << Qt::endl;

    for (const QString& error : result->errors) {
        err() << QStringLiteral("  ") << error << Qt::endl;
    }
    return result->errors.isEmpty() ? 0 : 1;
}

/// What is installed here, and how much of it can come with you.
///
/// The list matters more than it looks: "--apps a,b,c" needs ids, and there is
/// no other way to find out what they are. It also answers the question the
/// interface will ask in a table - whether an application's data travels at
/// all, or whether Transmit can only note that it was installed.
int runApps(bool onlyThoseThatCarryData) {
    auto platformService = platform::PlatformService::create();

    core::RecipeCatalog catalog;
    catalog.loadDefaults();

    const format::PathTokenMap folders = platformService->knownFolders();
    const format::OsFamily os = platformService->environment().os;

    QList<core::MatchedApp> matched = catalog.match(platformService->installedApplications(), os);
    matched += catalog.matchByStateOnly(matched, os, folders);
    catalog.noteWhichHaveState(matched, os, folders);

    std::sort(matched.begin(), matched.end(),
              [](const core::MatchedApp& a, const core::MatchedApp& b) {
                  return a.recipe.displayName.localeAwareCompare(b.recipe.displayName) < 0;
              });

    int shown = 0;
    for (const core::MatchedApp& match : matched) {
        const bool carries = match.recipe.portability.carriesData && match.hasState;
        if (onlyThoseThatCarryData && !carries) {
            continue;
        }
        ++shown;

        out() << QStringLiteral("%1  %2").arg(
                     carries ? QStringLiteral("data + list") : QStringLiteral("list only  "),
                     match.recipe.displayName)
              << Qt::endl;
        out() << QStringLiteral("    %1").arg(match.recipe.id) << Qt::endl;

        if (!carries && match.recipe.portability.carriesData) {
            out() << QStringLiteral(
                         "    its data could travel, but none of its folders are on this machine")
                  << Qt::endl;
        }
        for (const core::RecipeStatePath& root : match.recipe.state) {
            for (const QString& candidate : root.candidatesForOs(os)) {
                const QString absolute = core::RecipeCatalog::resolveStatePath(candidate, folders);
                if (!absolute.isEmpty() && QFileInfo::exists(absolute)) {
                    out() << QStringLiteral("    %1: %2").arg(root.id, absolute) << Qt::endl;
                    break;
                }
            }
        }

        const QString why = match.recipe.portability.reasonFor(os, os);
        if (!why.isEmpty()) {
            out() << QStringLiteral("    %1").arg(why) << Qt::endl;
        }
        out() << Qt::endl;
    }

    out() << QStringLiteral(
                 "%1 application(s)%2. Use the id with --apps, --no-app-data or "
                 "--app-roots.")
                 .arg(shown)
                 .arg(onlyThoseThatCarryData ? QStringLiteral(" whose data can travel") : QString())
          << Qt::endl;
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
        QStringLiteral("export, plan, import, inspect, verify, repair, rollback, apps, "
                       "profiles, drives or "
                       "environment"));
    parser.addPositionalArgument(
        QStringLiteral("archive"),
        QStringLiteral("archive path, for import, inspect, verify and repair"));

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
        QStringLiteral("Encryption passphrase. Every user on this machine can read it from the "
                       "process list, so prefer being asked for it."),
        QStringLiteral("text"));
    const QCommandLineOption passphraseFileOption(
        QStringLiteral("passphrase-file"), QStringLiteral("Read the passphrase from this file."),
        QStringLiteral("path"));
    const QCommandLineOption askPassphraseOption(
        QStringLiteral("ask-passphrase"),
        QStringLiteral("Ask for the passphrase on the terminal, without showing it. Happens by "
                       "itself when one is needed and none was given."));
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

    // Everything below is read back by name rather than being threaded through
    // runExport's parameter list, which was already ten long. A name is also
    // what the user typed, so a mistake here is visible in the same place.
    const QList<QCommandLineOption> scopeOptions = {
        QCommandLineOption(QStringLiteral("apps"),
                           QStringLiteral("Which applications' data to carry: all, none, or a "
                                          "comma-separated list of ids (see `apps`)."),
                           QStringLiteral("list")),
        QCommandLineOption(QStringLiteral("no-app-data"),
                           QStringLiteral("Note these applications as installed but leave their "
                                          "data behind."),
                           QStringLiteral("list")),
        QCommandLineOption(QStringLiteral("app-roots"),
                           QStringLiteral("Only these state folders of the chosen applications, "
                                          "by their id in the catalog."),
                           QStringLiteral("list")),
        QCommandLineOption(QStringLiteral("max-file-size"),
                           QStringLiteral("Leave out files larger than this, e.g. 2G."),
                           QStringLiteral("size")),
        QCommandLineOption(QStringLiteral("min-file-size"),
                           QStringLiteral("Leave out files smaller than this."),
                           QStringLiteral("size")),
        QCommandLineOption(QStringLiteral("modified-since"),
                           QStringLiteral("Only files touched since then: 30d, 6m, 2y, or a date."),
                           QStringLiteral("when")),
        QCommandLineOption(QStringLiteral("modified-before"),
                           QStringLiteral("Only files older than that."), QStringLiteral("when")),
        QCommandLineOption(QStringLiteral("include-ext"),
                           QStringLiteral("Only these file types, e.g. txt,md,pdf."),
                           QStringLiteral("list")),
        QCommandLineOption(QStringLiteral("exclude-ext"),
                           QStringLiteral("Leave out these file types."), QStringLiteral("list")),
        QCommandLineOption(QStringLiteral("exclude"),
                           QStringLiteral("Leave out paths matching this pattern. Repeatable."),
                           QStringLiteral("pattern")),
        QCommandLineOption(QStringLiteral("no-hidden"),
                           QStringLiteral("Leave out hidden files. Off by default, because on "
                                          "every system Transmit runs on that is where the "
                                          "settings are.")),
        QCommandLineOption(QStringLiteral("follow-symlinks"),
                           QStringLiteral("Copy what a link points at rather than the link.")),
        QCommandLineOption(QStringLiteral("block-size"),
                           QStringLiteral("How much is compressed together, e.g. 64M. Larger "
                                          "compresses better and costs more memory per worker."),
                           QStringLiteral("size")),
        QCommandLineOption(QStringLiteral("workers"),
                           QStringLiteral("Compression threads. 0 chooses from the machine."),
                           QStringLiteral("count")),
        QCommandLineOption(QStringLiteral("sync-every"),
                           QStringLiteral("Push this much to the drive at a time, e.g. 32M."),
                           QStringLiteral("size")),
        QCommandLineOption(QStringLiteral("no-verify-after"),
                           QStringLiteral("Do not read the archive back after writing it.")),
        QCommandLineOption(QStringLiteral("deep"),
                           QStringLiteral("For `verify`: check every file's contents against "
                                          "what was recorded, not only that each block "
                                          "decompresses.")),
        QCommandLineOption(QStringLiteral("json"),
                           QStringLiteral("For `verify`: write the result as JSON.")),
        QCommandLineOption(QStringLiteral("timings"),
                           QStringLiteral("Print where the time went, stage by stage.")),
        QCommandLineOption(QStringLiteral("from-report"),
                           QStringLiteral("For `repair`: recover the files a "
                                          "`verify --deep --json` run named, rather than "
                                          "checking the archive again."),
                           QStringLiteral("path")),
        QCommandLineOption(QStringLiteral("no-md5"),
                           QStringLiteral("Do not record an MD5 for each file. Saves 18 bytes a "
                                          "file and gives up being able to check the archive "
                                          "with md5sum.")),
        QCommandLineOption(QStringLiteral("no-md5-sidecar"),
                           QStringLiteral("Keep the per-file MD5s in the archive but do not "
                                          "write the .md5 file beside it.")),
        QCommandLineOption(QStringLiteral("md5-sidecar-names"),
                           QStringLiteral("List the file names in the .md5 file even when the "
                                          "archive is encrypted. They will be readable by "
                                          "anyone holding the drive.")),
        QCommandLineOption(QStringLiteral("selection-file"),
                           QStringLiteral("Read every choice from this file, then apply any "
                                          "options given here on top."),
                           QStringLiteral("path")),
        QCommandLineOption(QStringLiteral("carries-data-only"),
                           QStringLiteral("For `apps`: only the ones whose data can travel.")),
        QCommandLineOption(QStringLiteral("save-selection"),
                           QStringLiteral("Write the choices to this file so the same capture "
                                          "can be repeated."),
                           QStringLiteral("path")),
    };

    parser.addOptions({outputOption, profileOption, presetOption, splitOption, passphraseOption,
                       passphraseFileOption, askPassphraseOption, domainsOption, labelOption,
                       intoOption, emulateOption, dryRunOption, conflictOption, verifyOption,
                       verboseOption});
    parser.addOptions(scopeOptions);
    parser.process(app);

    core::configureLogging(parser.isSet(verboseOption));

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        parser.showHelp(1);
    }

    const QString command = positional.first();
    const QString archive = positional.size() > 1 ? positional.at(1) : QString();

    if (command == QLatin1String("export")) {
        return runExport(parser, outputOption, profileOption, presetOption, splitOption,
                         passphraseOption, passphraseFileOption, askPassphraseOption, domainsOption,
                         labelOption);
    }
    if (command == QLatin1String("import")) {
        if (archive.isEmpty()) {
            return reportError(QStringLiteral("import needs an archive path"));
        }
        return runImport(parser, archive, intoOption, passphraseOption, passphraseFileOption,
                         askPassphraseOption, emulateOption, dryRunOption, conflictOption,
                         domainsOption, verifyOption);
    }

    // The reading commands only need a passphrase when the archive has one,
    // which is why this is resolved here rather than for every command: asking
    // before `profiles` or `drives` would be nonsense.
    const auto passphraseFor = [&](const QString& path) {
        return resolvePassphrase(parser, passphraseFileOption, passphraseOption,
                                 askPassphraseOption, archiveIsEncrypted(path), false);
    };

    if (command == QLatin1String("inspect")) {
        if (archive.isEmpty()) {
            return reportError(QStringLiteral("inspect needs an archive path"));
        }
        return runInspect(archive, passphraseFor(archive));
    }
    if (command == QLatin1String("verify")) {
        if (archive.isEmpty()) {
            return reportError(QStringLiteral("verify needs an archive path"));
        }
        return runVerify(archive, passphraseFor(archive), parser.isSet(QStringLiteral("deep")),
                         parser.isSet(QStringLiteral("json")));
    }
    if (command == QLatin1String("plan")) {
        return runExport(parser, outputOption, profileOption, presetOption, splitOption,
                         passphraseOption, passphraseFileOption, askPassphraseOption, domainsOption,
                         labelOption, /*planOnly=*/true);
    }
    if (command == QLatin1String("repair")) {
        if (archive.isEmpty()) {
            return reportError(QStringLiteral("repair needs an archive path"));
        }
        return runRepair(archive, passphraseFor(archive),
                         parser.value(QStringLiteral("from-report")));
    }
    if (command == QLatin1String("rollback")) {
        if (archive.isEmpty()) {
            return reportError(QStringLiteral("rollback needs the path of an undo point"));
        }
        return runRollback(archive);
    }
    if (command == QLatin1String("apps")) {
        return runApps(parser.isSet(QStringLiteral("carries-data-only")));
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
