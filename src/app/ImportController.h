#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

#include "app/models/ContinuityReportModel.h"
#include "core/continuity/ContinuityTypes.h"
#include "core/services/ImportService.h"
#include "core/services/RollbackWriter.h"
#include "core/services/ScanService.h"
#include "platform/PlatformService.h"

namespace transmit::app {

/// Drives a restore from QML: inspecting an archive, previewing what a restore
/// would do, and carrying it out.
class ImportController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString archivePath READ archivePath NOTIFY summaryChanged)
    Q_PROPERTY(bool archiveValid READ archiveValid NOTIFY summaryChanged)
    Q_PROPERTY(bool archiveEncrypted READ archiveEncrypted NOTIFY summaryChanged)
    Q_PROPERTY(bool archiveUnlocked READ archiveUnlocked NOTIFY summaryChanged)
    Q_PROPERTY(QString archiveError READ archiveError NOTIFY summaryChanged)
    Q_PROPERTY(QString sourceDescription READ sourceDescription NOTIFY summaryChanged)
    Q_PROPERTY(QString capturedAtText READ capturedAtText NOTIFY summaryChanged)
    Q_PROPERTY(QString contentsText READ contentsText NOTIFY summaryChanged)
    Q_PROPERTY(bool crossPlatform READ isCrossPlatform NOTIFY summaryChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY progressChanged)
    Q_PROPERTY(QString currentItem READ currentItem NOTIFY progressChanged)
    Q_PROPERTY(double byteProgress READ byteProgress NOTIFY progressChanged)
    Q_PROPERTY(bool finished READ isFinished NOTIFY finishedChanged)
    Q_PROPERTY(bool succeeded READ succeeded NOTIFY finishedChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY finishedChanged)
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY finishedChanged)
    Q_PROPERTY(bool wasDryRun READ wasDryRun NOTIFY finishedChanged)
    Q_PROPERTY(QVariantMap archiveCounts READ archiveCounts NOTIFY archiveCountsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool undoing READ isUndoing NOTIFY undoChanged)
    Q_PROPERTY(QString undoSummary READ undoSummary NOTIFY undoChanged)
    Q_PROPERTY(QString undoDescription READ undoDescription NOTIFY undoChanged)

public:
    explicit ImportController(QObject* parent = nullptr);
    ~ImportController() override;

    [[nodiscard]] bool isRunning() const { return running_; }
    [[nodiscard]] QString archivePath() const { return archivePath_; }
    [[nodiscard]] bool archiveValid() const { return summary_.valid; }
    [[nodiscard]] bool archiveEncrypted() const { return summary_.encrypted; }
    [[nodiscard]] bool archiveUnlocked() const { return summary_.unlocked; }
    [[nodiscard]] QString archiveError() const { return summary_.errorMessage; }
    [[nodiscard]] QString sourceDescription() const;
    [[nodiscard]] QString capturedAtText() const;
    [[nodiscard]] QString contentsText() const;
    [[nodiscard]] bool isCrossPlatform() const;
    [[nodiscard]] QString stage() const { return progress_.stage; }
    [[nodiscard]] QString currentItem() const { return progress_.currentItem; }
    [[nodiscard]] double byteProgress() const;
    [[nodiscard]] bool isFinished() const { return finished_; }
    [[nodiscard]] bool succeeded() const { return report_.succeeded; }
    [[nodiscard]] QString errorMessage() const { return report_.errorMessage; }
    [[nodiscard]] QString summaryText() const;
    [[nodiscard]] bool wasDryRun() const { return wasDryRun_; }

    /// Root path to the number of archives found there, for drives that have
    /// been looked at. A drive missing from the map has not been read yet.
    [[nodiscard]] QVariantMap archiveCounts() const { return archiveCounts_; }

    /// True while there is an undo point for the last restore that has neither
    /// been used nor thrown away.
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool isUndoing() const { return undoing_; }

    /// What the undo did, once it has run. Empty before that.
    [[nodiscard]] QString undoSummary() const { return undoSummary_; }

    /// What undoing would do, in the user's terms.
    [[nodiscard]] QString undoDescription() const;

    [[nodiscard]] const core::ImportReport& report() const { return report_; }

public slots:
    /// Reads an archive's header and, if it opens, its manifest.
    void inspect(const QString& archivePath, const QString& passphrase = {});

    /// Starts reading a folder for archives, off the interface thread.
    ///
    /// Listing a directory sounds cheap until the directory is a USB drive
    /// that has spun down or a network share that has gone away, at which
    /// point it blocks for as long as the kernel takes to give up. Doing that
    /// from a binding would freeze the window, so the result arrives later
    /// through archiveCounts and archivesOn.
    void scanForArchives(const QString& folder);

    /// What the last scan of this folder found. Empty for a folder that has
    /// not been scanned, which is why archiveCounts is what the interface
    /// binds to - it can tell "none" from "not yet".
    [[nodiscard]] QStringList archivesOn(const QString& folder) const;

    /// Puts the machine back the way it was before the last restore.
    ///
    /// Every restore writes an undo point before it touches anything, which
    /// until now nothing in the interface could reach - so the application was
    /// quietly leaving an archive in the user's home that only the command
    /// line could use.
    void undoLastRestore();

    /// Accepts the restore: the undo point and the kept originals of every
    /// rewritten file are deleted. Nothing that was restored is touched.
    void keepLastRestore();

    /// Runs the restore. With `dryRun` nothing is written and the report says
    /// what would have happened.
    void start(const QString& passphrase, const QString& conflictPolicy, bool dryRun,
               bool verifyFirst, const QString& destinationOverride);

    void cancel();

    /// Fills the shared report model with this run's notes. QML passes the
    /// model in, so one report view serves both capture and restore.
    void populateReport(ContinuityReportModel* model) const;
    void reset();

signals:
    void runningChanged();
    void summaryChanged();
    void progressChanged();
    void finishedChanged();
    void archiveCountsChanged();
    void undoChanged();
    void reportReady();

private:
    void handleProgress(const core::ProgressUpdate& update);
    void handleFinished();
    void recordScan(const QString& folder, const QStringList& archives);
    void handleUndoFinished();

    /// Deletes the undo point, and the directory holding it if that leaves it
    /// empty. Used by both answers - undoing consumes it, keeping discards it.
    void forgetUndoPoint();

    std::unique_ptr<platform::PlatformService> platform_;
    std::unique_ptr<core::ImportService> service_;
    core::CancelToken cancelToken_;

    QFutureWatcher<core::ImportReport> watcher_;
    core::ArchiveSummary summary_;
    core::ProgressUpdate progress_;
    core::ImportReport report_;
    QString archivePath_;
    bool running_ = false;
    bool finished_ = false;
    bool wasDryRun_ = false;

    QFutureWatcher<format::Result<core::RollbackWriter::UndoResult>> undoWatcher_;
    QString undoSummary_;
    bool undoing_ = false;
    bool undoUsed_ = false;

    QHash<QString, QStringList> archivesByFolder_;
    QSet<QString> scansInFlight_;
    QVariantMap archiveCounts_;
};

}  // namespace transmit::app
