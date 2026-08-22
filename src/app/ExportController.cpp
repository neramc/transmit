#include "app/ExportController.h"

#include <QDateTime>
#include <QDir>
#include <QtConcurrent/QtConcurrentRun>

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
                                                      bool includeSecrets) {
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
    return selection;
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
    request.partSize = splitForFat32 ? format::kFat32SafePartSize : 0;

    if (const auto parsed = format::presetFromName(core::toUtf8(preset))) {
        request.preset = *parsed;
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
