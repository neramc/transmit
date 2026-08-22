#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <memory>

#include "core/continuity/ContinuityTypes.h"
#include "app/models/ContinuityReportModel.h"
#include "core/services/ImportService.h"
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

    [[nodiscard]] const core::ImportReport& report() const { return report_; }

public slots:
    /// Reads an archive's header and, if it opens, its manifest.
    void inspect(const QString& archivePath, const QString& passphrase = {});

    /// Searches a folder for archives, returning the paths found. Used to spot
    /// a capture on a freshly inserted stick.
    [[nodiscard]] QStringList findArchives(const QString& folder) const;

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
    void reportReady();

private:
    void handleProgress(const core::ProgressUpdate& update);
    void handleFinished();

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
};

}  // namespace transmit::app
