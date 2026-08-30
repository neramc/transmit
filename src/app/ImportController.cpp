#include "app/ImportController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QtConcurrent/QtConcurrentRun>

#include "core/rewrite/RewritePlan.h"
#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"

namespace transmit::app {
namespace {

using core::formatBytes;

/// Runs on a worker thread, so it touches nothing but its argument.
QStringList listArchivesIn(const QString& folder) {
    QStringList found;
    if (folder.isEmpty()) {
        return found;
    }

    // A split archive is offered by its first part only; opening that one finds
    // the rest of the set.
    const QDir directory(folder);
    for (const QString& name : directory.entryList(
             {QStringLiteral("*.txa"), QStringLiteral("*.txa.001")}, QDir::Files, QDir::Name)) {
        found.push_back(directory.absoluteFilePath(name));
    }
    return found;
}

core::ConflictPolicy policyFromName(const QString& name) {
    if (name == QLatin1String("skip"))
        return core::ConflictPolicy::Skip;
    if (name == QLatin1String("overwrite"))
        return core::ConflictPolicy::Overwrite;
    if (name == QLatin1String("newer"))
        return core::ConflictPolicy::NewerWins;
    return core::ConflictPolicy::KeepBoth;
}

}  // namespace

ImportController::ImportController(QObject* parent) : QObject(parent) {
    platform_ = platform::PlatformService::create();
    service_ = std::make_unique<core::ImportService>(*platform_);

    connect(&watcher_, &QFutureWatcher<core::ImportReport>::finished, this,
            &ImportController::handleFinished);
    connect(&inspectWatcher_, &QFutureWatcher<core::ArchiveSummary>::finished, this, [this]() {
        summary_ = inspectWatcher_.result();
        inspecting_ = false;
        emit inspectingChanged();
        emit summaryChanged();
    });
    connect(&undoWatcher_,
            &QFutureWatcher<format::Result<core::RollbackWriter::UndoResult>>::finished, this,
            &ImportController::handleUndoFinished);
}

ImportController::~ImportController() {
    cancelToken_.cancel();
    watcher_.waitForFinished();
    undoWatcher_.waitForFinished();

    // The inspection holds a raw pointer to the service, which is about to go
    // away with this object.
    inspectWatcher_.waitForFinished();
}

bool ImportController::canUndo() const {
    // Deliberately not gated on success. A restore that half worked is
    // exactly when somebody wants the machine put back, and gating this
    // on report_.succeeded took undo away in the one case it is for.
    return !undoUsed_ && !undoing_ && finished_ && !wasDryRun_ &&
           !report_.rollbackArchivePath.isEmpty();
}

QString ImportController::undoDescription() const {
    if (report_.rollbackArchivePath.isEmpty()) {
        return {};
    }
    if (report_.rewrittenFiles.isEmpty()) {
        return tr(
            "Puts back everything this restore replaced and removes what it added. "
            "Programs you installed yourself are not touched.");
    }
    return tr(
        "Puts back everything this restore replaced, removes what it added, and undoes "
        "the %n settings file(s) whose folder names were corrected.",
        nullptr, static_cast<int>(report_.rewrittenFiles.size()));
}

void ImportController::undoLastRestore() {
    if (!canUndo()) {
        return;
    }

    undoing_ = true;
    undoSummary_.clear();
    emit undoChanged();

    const QString archive = report_.rollbackArchivePath;
    undoWatcher_.setFuture(
        QtConcurrent::run([archive]() { return core::RollbackWriter::undo(archive); }));
}

void ImportController::handleUndoFinished() {
    const auto result = undoWatcher_.result();
    undoing_ = false;

    if (!result) {
        undoSummary_ =
            tr("The undo could not be read: %1").arg(core::describeError(result.error()));
        qCWarning(logRestore) << "undo failed:" << undoSummary_;
        emit undoChanged();
        return;
    }

    undoUsed_ = true;

    undoSummary_ = tr("Put back %n file(s)", nullptr, result->filesRestored) +
                   tr(" and removed %n that the restore had added.", nullptr, result->filesRemoved);
    for (const QString& error : result->errors) {
        undoSummary_ += QLatin1Char('\n') + error;
    }

    if (result->errors.isEmpty()) {
        forgetUndoPoint();
    } else {
        // The archive holds the only remaining copy of whatever could not
        // be put back. Deleting it here - which is what used to happen,
        // errors or not - threw those originals away for good, and a
        // locked file is the expected failure, not a rare one.
        undoSummary_ +=
            QLatin1Char('\n') +
            tr("The undo point is kept at %1. Close whatever is holding those files and run "
               "\"transmit-cli rollback\" on it again.")
                .arg(report_.rollbackArchivePath);
    }

    qCInfo(logRestore) << "undo restored" << result->filesRestored << "and removed"
                       << result->filesRemoved;
    emit undoChanged();
}

void ImportController::forgetUndoPoint() {
    if (report_.rollbackArchivePath.isEmpty()) {
        return;
    }
    QFile::remove(report_.rollbackArchivePath);

    // rmdir only succeeds on an empty directory, which is exactly the
    // condition for removing it: anything else in there was not ours.
    QDir().rmdir(QFileInfo(report_.rollbackArchivePath).absolutePath());
}

void ImportController::keepLastRestore() {
    if (!canUndo()) {
        return;
    }

    const int discarded = core::RewritePlan::discardBackups(report_.rewrittenFiles);
    forgetUndoPoint();

    undoUsed_ = true;
    undoSummary_ = discarded == 0
                       ? tr("Kept. The undo point has been deleted.")
                       : tr("Kept. The undo point and %n kept original(s) have been deleted.",
                            nullptr, discarded);
    qCInfo(logRestore) << "restore accepted;" << discarded << "backup(s) discarded";
    emit undoChanged();
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
    // Off the interface thread. Opening an archive reads the header and the
    // manifest, and for an encrypted one derives the key with scrypt at 2^17,
    // which is about a second by design - a second during which the window
    // would not repaint, would not respond to a click, and would be reported
    // by the desktop as not responding.
    if (inspecting_) {
        return;
    }
    archivePath_ = archivePath;
    inspecting_ = true;
    emit inspectingChanged();

    // The service is used from the worker, so nothing else may touch it until
    // the result arrives; the guard above is what ensures that.
    core::ImportService* const service = service_.get();
    inspectWatcher_.setFuture(QtConcurrent::run(
        [service, archivePath, passphrase] { return service->inspect(archivePath, passphrase); }));
}

void ImportController::scanForArchives(const QString& folder) {
    if (folder.isEmpty() || scansInFlight_.contains(folder)) {
        return;
    }
    scansInFlight_.insert(folder);

    auto* const watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher, folder]() {
        recordScan(folder, watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&listArchivesIn, folder));
}

QStringList ImportController::archivesOn(const QString& folder) const {
    return archivesByFolder_.value(folder);
}

void ImportController::recordScan(const QString& folder, const QStringList& archives) {
    scansInFlight_.remove(folder);

    const auto existing = archivesByFolder_.constFind(folder);
    if (existing != archivesByFolder_.cend() && *existing == archives) {
        return;
    }

    archivesByFolder_.insert(folder, archives);
    archiveCounts_.insert(folder, archives.size());
    qCDebug(logRestore) << "found" << archives.size() << "archive(s) on" << folder;
    emit archiveCountsChanged();
}

void ImportController::lookForInterruptedRestore(const QString& destinationOverride) {
    const bool wasOffered = interrupted_.found;

    interrupted_ = {};
    carryOnText_.clear();
    carryingOn_ = false;

    if (!archivePath_.isEmpty()) {
        interrupted_ = service_->findInterruptedRestore(archivePath_, destinationOverride);
    }

    if (interrupted_.found) {
        carryOnText_ =
            tr("A restore of this archive here stopped part way, with %n item(s) already in "
               "place. It can be finished rather than started again.",
               nullptr, static_cast<int>(interrupted_.itemsAlreadyInPlace));
    }

    // Said only when it changes. A binding that fires on every folder the user
    // clicks through would redraw the offer for folders that never had one.
    if (wasOffered != interrupted_.found) {
        emit carryOnChanged();
    }
}

void ImportController::carryOn() {
    if (!interrupted_.found || carryingOn_) {
        return;
    }
    carryingOn_ = true;
    emit carryOnChanged();
}

void ImportController::startFresh() {
    if (!interrupted_.found && !carryingOn_) {
        return;
    }
    interrupted_ = {};
    carryOnText_.clear();
    carryingOn_ = false;
    emit carryOnChanged();
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
    request.resume = carryingOn_;

    running_ = true;
    undoUsed_ = false;
    undoSummary_.clear();
    emit runningChanged();
    emit progressChanged();
    emit undoChanged();

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

    // Whatever the run did, the offer that was standing before it is spent:
    // a run that finished consumed the record, and a run that stopped part
    // way wrote a new one describing itself rather than its predecessor. The
    // report's own canBeCarriedOn is what the finished page reads; a fresh
    // offer comes from looking again.
    const bool wasOffered = interrupted_.found || carryingOn_;
    interrupted_ = {};
    carryOnText_.clear();
    carryingOn_ = false;

    emit runningChanged();
    emit finishedChanged();
    emit undoChanged();
    if (wasOffered) {
        emit carryOnChanged();
    }
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

    // The offer goes with it. It was about one archive into one folder, and
    // after a reset the interface is asking about neither yet.
    startFresh();

    emit progressChanged();
    emit finishedChanged();
}

}  // namespace transmit::app
