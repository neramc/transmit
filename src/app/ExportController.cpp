#include "app/ExportController.h"

#include <QDateTime>
#include <QDir>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

#include "core/secrets/SecretsDomain.h"
#include "core/services/ProfileService.h"
#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"

namespace transmit::app {
namespace {

using core::formatBytes;
using core::formatDuration;

/// A file name a user can recognise on a stick holding several captures.
QString suggestFileName(const platform::EnvironmentInfo& environment) {
    QString host =
        environment.hostName.isEmpty() ? QStringLiteral("machine") : environment.hostName;
    host.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("-"));
    return QStringLiteral("%1-%2.txa")
        .arg(host, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmm")));
}

}  // namespace

ExportController::ExportController(QObject* parent) : QObject(parent) {
    platform_ = platform::PlatformService::create();
    service_ = std::make_unique<core::ExportService>(*platform_);

    connect(&watcher_, &QFutureWatcher<core::ExportReport>::finished, this,
            &ExportController::handleFinished);
    connect(&programWatcher_, &QFutureWatcher<QList<platform::RunningApp>>::finished, this,
            &ExportController::handleProgramCheckFinished);
}

ExportController::~ExportController() {
    cancelToken_.cancel();
    watcher_.waitForFinished();
    programWatcher_.waitForFinished();
}

core::CaptureSelection ExportController::selectionFor(const QString& profileId,
                                                      const QStringList& domains,
                                                      bool includeSecrets) const {
    core::CaptureSelection selection = core::ProfileService::profileById(profileId).selection;

    if (!domains.isEmpty()) {
        selection.domains.clear();
        for (const QString& name : domains) {
            if (const auto domain = format::domainFromName(core::toUtf8(name))) {
                selection.domains.insert(static_cast<int>(*domain));
            }
        }
    }
    if (includeSecrets) {
        selection.domains.insert(static_cast<int>(format::DomainId::Secrets));
    }

    // The profile decides which folders; these decide how much of them. Kept
    // apart deliberately, so switching profile does not silently discard the
    // limits somebody set, and narrowing the scope does not change what a
    // profile means.
    selection.scope = scope_;
    selection.appMode = appMode_;
    selection.apps = apps_;
    return selection;
}

void ExportController::chooseApplications(AppCatalogModel* model) {
    if (model == nullptr) {
        return;
    }
    apps_ = model->selection();
    appMode_ = core::AppSelectionMode::Explicit;

    appsOffered_ = model->carriesDataCount();
    appsChosen_ = static_cast<int>(
        std::count_if(apps_.constBegin(), apps_.constEnd(),
                      [](const core::AppSelection& app) { return app.captureState; }));

    // The pre-flight answer was about a different set of applications, so it
    // is no longer an answer to this question.
    forgetRunningPrograms();
    emit scopeChanged();
}

void ExportController::carryEveryApplication() {
    if (appMode_ == core::AppSelectionMode::All && apps_.isEmpty()) {
        return;
    }
    apps_.clear();
    appMode_ = core::AppSelectionMode::All;
    appsChosen_ = 0;
    appsOffered_ = 0;
    forgetRunningPrograms();
    emit scopeChanged();
}

void ExportController::setScope(double maximumFileSize, int modifiedWithinDays,
                                const QString& excludedExtensions) {
    core::ScopeRule rule;

    // Guarded rather than cast straight through: QML hands this over as a
    // double, and a negative or non-finite one would wrap to an enormous
    // limit - which reads as "no limit" and is the opposite of what somebody
    // dragging a size control down would mean.
    if (std::isfinite(maximumFileSize) && maximumFileSize >= 1.0) {
        rule.maximumFileSize = static_cast<quint64>(maximumFileSize);
    }
    if (modifiedWithinDays > 0) {
        rule.modifiedSince = QDateTime::currentDateTime().addDays(-modifiedWithinDays);
    }
    for (const QString& piece : excludedExtensions.split(
             QRegularExpression(QStringLiteral("[^A-Za-z0-9_+-]+")), Qt::SkipEmptyParts)) {
        rule.excludeExtensions.insert(piece.toLower());
    }

    scope_ = rule;
    emit scopeChanged();
}

void ExportController::clearScope() {
    if (scope_.isUnrestricted()) {
        return;
    }
    scope_ = {};
    emit scopeChanged();
}

QString ExportController::verificationText() const {
    if (!report_.verificationRan) {
        return {};
    }
    if (!report_.verified) {
        return tr("%1 of %2 files did not read back from the drive correctly.")
            .arg(report_.verificationFailures)
            .arg(report_.verifiedFiles);
    }

    QString text = tr("Read back off the drive: all %1 files matched.").arg(report_.verifiedFiles);
    if (!report_.verificationUsedColdReads) {
        // Not a footnote. Without the eviction the read-back may have been
        // served from memory, and somebody about to wipe this machine is
        // entitled to know that before they do.
        text += QLatin1Char(' ');
        text +=
            tr("This system cannot be asked to forget its cached copy of a file, so some of "
               "that may have been read from memory rather than from the drive.");
    }
    if (report_.verificationRetriedReads > 0) {
        text += QLatin1Char(' ');
        text += tr("%1 read(s) only worked after retrying - the archive is sound, but a drive "
                   "that needs a second attempt is worth replacing.")
                    .arg(report_.verificationRetriedReads);
    }
    return text;
}

QString ExportController::scopeSummary() const {
    if (scope_.isUnrestricted()) {
        return tr("Everything in the folders you chose");
    }

    QStringList parts;
    if (scope_.maximumFileSize > 0) {
        parts << tr("nothing over %1").arg(formatBytes(scope_.maximumFileSize));
    }
    if (scope_.modifiedSince.isValid()) {
        const qint64 days = scope_.modifiedSince.daysTo(QDateTime::currentDateTime());
        parts << (days == 1 ? tr("nothing untouched since yesterday")
                            : tr("nothing untouched for %1 days").arg(days));
    }
    if (!scope_.excludeExtensions.isEmpty()) {
        QStringList kinds(scope_.excludeExtensions.constBegin(),
                          scope_.excludeExtensions.constEnd());
        kinds.sort();
        parts << tr("no .%1 files").arg(kinds.join(QStringLiteral(", .")));
    }
    return parts.join(QStringLiteral("; "));
}

QString ExportController::applicationSummary() const {
    if (appMode_ != core::AppSelectionMode::Explicit) {
        return tr("Every program whose data can travel");
    }
    if (appsChosen_ == 0) {
        return tr("No program data - only the list of what you had installed");
    }
    if (appsOffered_ > 0 && appsChosen_ >= appsOffered_) {
        return appsChosen_ == 1 ? tr("The one program whose data can travel")
                                : tr("All %1 programs whose data can travel").arg(appsChosen_);
    }
    return tr("%1 of %2 programs").arg(appsChosen_).arg(appsOffered_);
}

void ExportController::checkForRunningPrograms(const QString& profileId,
                                               const QStringList& domains) {
    if (checkingPrograms_ || running_) {
        return;
    }
    checkingPrograms_ = true;
    emit programsToCloseChanged();

    core::ExportService* const service = service_.get();
    const core::CaptureSelection selection = selectionFor(profileId, domains, false);
    programWatcher_.setFuture(QtConcurrent::run(
        [service, selection]() { return service->applicationsToClose(selection); }));
}

void ExportController::handleProgramCheckFinished() {
    const QList<platform::RunningApp> running = programWatcher_.result();

    programsToClose_.clear();
    for (const platform::RunningApp& app : running) {
        const QString name = app.displayName.isEmpty() ? app.processName : app.displayName;
        if (!programsToClose_.contains(name)) {
            programsToClose_.push_back(name);
        }
    }
    programsToClose_.sort(Qt::CaseInsensitive);

    checkingPrograms_ = false;
    programsChecked_ = true;
    qCInfo(logCapture) << programsToClose_.size() << "program(s) should be closed first";
    emit programsToCloseChanged();
}

void ExportController::forgetRunningPrograms() {
    if (!programsChecked_ && programsToClose_.isEmpty()) {
        return;
    }
    programsToClose_.clear();
    programsChecked_ = false;
    emit programsToCloseChanged();
}

double ExportController::fileProgress() const {
    return core::percentage(progress_.filesDone, progress_.filesTotal) / 100.0;
}

double ExportController::byteProgress() const {
    return core::percentage(progress_.bytesDone, progress_.bytesTotal) / 100.0;
}

QString ExportController::bytesReadText() const {
    if (progress_.bytesTotal == 0) {
        return formatBytes(progress_.bytesDone);
    }
    return tr("%1 of %2").arg(formatBytes(progress_.bytesDone), formatBytes(progress_.bytesTotal));
}

QString ExportController::bytesWrittenText() const {
    return formatBytes(progress_.bytesStored);
}

QString ExportController::compressionText() const {
    if (progress_.bytesDone == 0 || progress_.bytesStored == 0) {
        return {};
    }
    const double ratio =
        static_cast<double>(progress_.bytesStored) / static_cast<double>(progress_.bytesDone);
    return tr("%1% of the original size").arg(ratio * 100.0, 0, 'f', 1);
}

QString ExportController::etaText() const {
    if (!running_ || progress_.bytesDone == 0 || progress_.bytesTotal == 0) {
        return {};
    }
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startedAtMs_;
    if (elapsed < 2000) {
        return {};  // too early for a number worth showing
    }
    const double fraction =
        static_cast<double>(progress_.bytesDone) / static_cast<double>(progress_.bytesTotal);
    if (fraction <= 0.0) {
        return {};
    }
    const auto remaining = static_cast<qint64>(static_cast<double>(elapsed) / fraction) - elapsed;
    return formatDuration(remaining);
}

QString ExportController::summaryText() const {
    if (!report_.succeeded) {
        return {};
    }
    QString text = tr("%1 files, %2 folders").arg(report_.fileCount).arg(report_.directoryCount);
    text +=
        tr(" - %1 became %2").arg(formatBytes(report_.rawBytes), formatBytes(report_.storedBytes));
    if (report_.deduplicatedBytes > 0) {
        text += tr(", saving %1 on repeated content").arg(formatBytes(report_.deduplicatedBytes));
    }
    return text;
}

QString ExportController::incompleteText() const {
    if (!report_.incomplete) {
        return {};
    }
    // Written out rather than left as Qt's untranslated plural form, which
    // renders "3 folder(s)" - a warning about an incomplete backup should not
    // read like debug output.
    const int folders = static_cast<int>(report_.unreadablePaths.size());
    const QString count = folders == 1 ? tr("One folder") : tr("%1 folders").arg(folders);
    return tr("%1 could not be opened, so nothing inside them was captured. This archive is not "
              "a complete copy of what you selected.")
        .arg(count);
}

quint64 ExportController::estimateSize(const QString& profileId) {
    const core::CaptureProfile profile = core::ProfileService::profileById(profileId);
    core::CancelToken token;
    return service_->estimateSize(profile.selection, token);
}

QStringList ExportController::domainsForProfile(const QString& profileId) {
    const core::CaptureProfile profile = core::ProfileService::profileById(profileId);

    QStringList names;
    for (const format::DomainId domain : format::allDomains()) {
        if (profile.selection.includes(domain)) {
            names << core::fromUtf8(format::domainName(domain));
        }
    }
    return names;
}

bool ExportController::secretsAvailable() {
    return core::SecretsDomain(*platform_).isAvailable();
}

QString ExportController::secretsStoreName() {
    return core::SecretsDomain(*platform_).describeStore();
}

void ExportController::start(const QString& profileId, const QString& destinationFolder,
                             const QString& preset, const QString& passphrase, bool splitForFat32,
                             const QString& label, const QStringList& domains,
                             bool includeSecrets) {
    if (running_) {
        return;
    }

    cancelToken_.reset();
    progress_ = {};
    report_ = {};
    finished_ = false;
    startedAtMs_ = QDateTime::currentMSecsSinceEpoch();

    core::ExportRequest request;
    request.label = label;
    request.selection = selectionFor(profileId, domains, includeSecrets);

    request.passphrase = passphrase;
    request.packaging.partSize = splitForFat32 ? format::kFat32SafePartSize : 0;

    if (const auto parsed = format::presetFromName(core::toUtf8(preset))) {
        request.packaging.preset = *parsed;
    }

    const QString folder = destinationFolder.isEmpty() ? QDir::homePath() : destinationFolder;
    request.destinationPath = QDir(folder).filePath(suggestFileName(platform_->environment()));

    running_ = true;
    emit runningChanged();
    emit progressChanged();

    // The progress callback runs on the worker thread; emitting a signal from
    // there is safe, and Qt queues the delivery to whichever thread is
    // listening.
    auto* service = service_.get();
    core::CancelToken* token = &cancelToken_;
    watcher_.setFuture(QtConcurrent::run([this, service, token, request]() {
        return service->run(request, *token,
                            [this](const core::ProgressUpdate& update) { handleProgress(update); });
    }));
}

void ExportController::handleProgress(const core::ProgressUpdate& update) {
    progress_ = update;
    emit progressChanged();
}

void ExportController::handleFinished() {
    report_ = watcher_.result();
    running_ = false;
    finished_ = true;

    emit runningChanged();
    emit finishedChanged();
    emit reportReady();
}

void ExportController::populateReport(ContinuityReportModel* model) const {
    if (model != nullptr) {
        model->setNotes(report_.notes);
    }
}

void ExportController::cancel() {
    if (running_) {
        cancelToken_.cancel();
        qCInfo(logUi) << "capture cancelled by the user";
    }
}

void ExportController::reset() {
    if (running_) {
        return;
    }
    progress_ = {};
    report_ = {};
    finished_ = false;
    emit progressChanged();
    emit finishedChanged();
}

}  // namespace transmit::app
