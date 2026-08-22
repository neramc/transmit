#include "app/ImportController.h"

#include <QDir>
#include <QLocale>
#include <QtConcurrent/QtConcurrentRun>

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"

namespace transmit::app {
namespace {

using core::formatBytes;

core::ConflictPolicy policyFromName(const QString& name) {
    if (name == QLatin1String("skip")) return core::ConflictPolicy::Skip;
    if (name == QLatin1String("overwrite")) return core::ConflictPolicy::Overwrite;
    if (name == QLatin1String("newer")) return core::ConflictPolicy::NewerWins;
    return core::ConflictPolicy::KeepBoth;
}

}  // namespace

ImportController::ImportController(QObject* parent) : QObject(parent) {
    platform_ = platform::PlatformService::create();
    service_ = std::make_unique<core::ImportService>(*platform_);

    connect(&watcher_, &QFutureWatcher<core::ImportReport>::finished, this,
            &ImportController::handleFinished);
}

ImportController::~ImportController() {
    cancelToken_.cancel();
    watcher_.waitForFinished();
}

QString ImportController::sourceDescription() const {
    if (!summary_.unlocked) {
        return {};
    }
    if (summary_.sourceHost.isEmpty()) {
        return summary_.sourceOsName;
    }
    return tr("%1 on %2").arg(summary_.sourceOsName, summary_.sourceHost);
}

QString ImportController::capturedAtText() const {
    if (!summary_.capturedAt.isValid()) {
        return {};
    }
    return QLocale().toString(summary_.capturedAt, QLocale::LongFormat);
}

QString ImportController::contentsText() const {
    if (!summary_.unlocked) {
        return {};
    }
    return tr("%1 files, %2").arg(summary_.fileCount).arg(formatBytes(summary_.rawBytes));
}

bool ImportController::isCrossPlatform() const {
    return summary_.unlocked && summary_.sourceOs != format::OsFamily::Unknown &&
           summary_.sourceOs != platform_->environment().os;
}

double ImportController::byteProgress() const {
    return core::percentage(progress_.bytesDone, progress_.bytesTotal) / 100.0;
}

QString ImportController::summaryText() const {
    if (!report_.succeeded) {
        return {};
    }
    if (wasDryRun_) {
        return tr("%1 items would be restored, %2 left alone")
            .arg(report_.filesRestored)
            .arg(report_.filesSkipped);
    }
    return tr("%1 items restored (%2), %3 left alone")
        .arg(report_.filesRestored)
        .arg(formatBytes(report_.bytesWritten))
        .arg(report_.filesSkipped);
}

void ImportController::inspect(const QString& archivePath, const QString& passphrase) {
    archivePath_ = archivePath;
    summary_ = service_->inspect(archivePath, passphrase);
    emit summaryChanged();
}

QStringList ImportController::findArchives(const QString& folder) const {
    QStringList found;
    if (folder.isEmpty()) {
        return found;
    }

    // A split archive is offered by its first part only; opening that one finds
    // the rest of the set.
    const QDir directory(folder);
    for (const QString& name :
         directory.entryList({QStringLiteral("*.txa"), QStringLiteral("*.txa.001")},
                             QDir::Files, QDir::Name)) {
        found.push_back(directory.absoluteFilePath(name));
    }
    return found;
}

void ImportController::start(const QString& passphrase, const QString& conflictPolicy, bool dryRun,
                             bool verifyFirst, const QString& destinationOverride) {
    if (running_ || archivePath_.isEmpty()) {
        return;
    }

    cancelToken_.reset();
    progress_ = {};
    report_ = {};
    finished_ = false;
    wasDryRun_ = dryRun;

    core::ImportRequest request;
    request.archivePath = archivePath_;
    request.passphrase = passphrase;
    request.conflictPolicy = policyFromName(conflictPolicy);
    request.dryRun = dryRun;
    request.verifyFirst = verifyFirst;
    request.destinationOverride = destinationOverride;

    running_ = true;
    emit runningChanged();
    emit progressChanged();

    auto* service = service_.get();
    core::CancelToken* token = &cancelToken_;
    watcher_.setFuture(QtConcurrent::run([this, service, token, request]() {
        return service->run(request, *token,
                            [this](const core::ProgressUpdate& update) { handleProgress(update); });
    }));
}

void ImportController::handleProgress(const core::ProgressUpdate& update) {
    progress_ = update;
    emit progressChanged();
}

void ImportController::handleFinished() {
    report_ = watcher_.result();
    running_ = false;
    finished_ = true;

    emit runningChanged();
    emit finishedChanged();
    emit reportReady();
}

void ImportController::populateReport(ContinuityReportModel* model) const {
    if (model != nullptr) {
        model->setNotes(report_.notes);
    }
}

void ImportController::cancel() {
    if (running_) {
        cancelToken_.cancel();
        qCInfo(logUi) << "restore cancelled by the user";
    }
}

void ImportController::reset() {
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
