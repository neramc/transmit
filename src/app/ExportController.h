#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <memory>

#include "app/models/ContinuityReportModel.h"
#include "core/continuity/ContinuityTypes.h"
#include "core/services/ExportService.h"
#include "core/services/ScanService.h"
#include "platform/PlatformService.h"

namespace transmit::app {

/// Drives a capture from QML.
///
/// The service itself is synchronous; this class owns the worker thread, the
/// cancellation token and the properties the interface binds to. Nothing here
/// makes a decision about what to capture - that is the service's job - so the
/// same logic runs identically from the command line.
class ExportController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QStringList programsToClose READ programsToClose NOTIFY programsToCloseChanged)
    Q_PROPERTY(bool checkingPrograms READ checkingPrograms NOTIFY programsToCloseChanged)
    Q_PROPERTY(bool programsChecked READ programsChecked NOTIFY programsToCloseChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY progressChanged)
    Q_PROPERTY(QString currentItem READ currentItem NOTIFY progressChanged)
    Q_PROPERTY(double fileProgress READ fileProgress NOTIFY progressChanged)
    Q_PROPERTY(double byteProgress READ byteProgress NOTIFY progressChanged)
    Q_PROPERTY(QString bytesReadText READ bytesReadText NOTIFY progressChanged)
    Q_PROPERTY(QString bytesWrittenText READ bytesWrittenText NOTIFY progressChanged)
    Q_PROPERTY(QString compressionText READ compressionText NOTIFY progressChanged)
    Q_PROPERTY(QString etaText READ etaText NOTIFY progressChanged)
    Q_PROPERTY(bool finished READ isFinished NOTIFY finishedChanged)
    Q_PROPERTY(bool succeeded READ succeeded NOTIFY finishedChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY finishedChanged)
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY finishedChanged)
    Q_PROPERTY(QStringList archiveParts READ archiveParts NOTIFY finishedChanged)

public:
    explicit ExportController(QObject* parent = nullptr);
    ~ExportController() override;

    [[nodiscard]] bool isRunning() const { return running_; }
    [[nodiscard]] QString stage() const { return progress_.stage; }
    [[nodiscard]] QString currentItem() const { return progress_.currentItem; }
    [[nodiscard]] double fileProgress() const;
    [[nodiscard]] double byteProgress() const;
    [[nodiscard]] QString bytesReadText() const;
    [[nodiscard]] QString bytesWrittenText() const;
    [[nodiscard]] QString compressionText() const;
    [[nodiscard]] QString etaText() const;
    [[nodiscard]] bool isFinished() const { return finished_; }
    [[nodiscard]] bool succeeded() const { return report_.succeeded; }
    [[nodiscard]] QString errorMessage() const { return report_.errorMessage; }
    [[nodiscard]] QString summaryText() const;
    [[nodiscard]] QStringList archiveParts() const { return report_.archiveParts; }

    [[nodiscard]] const core::ExportReport& report() const { return report_; }

    /// Programs the user would do well to close first, by the name they would
    /// recognise. Empty until a check has run.
    [[nodiscard]] QStringList programsToClose() const { return programsToClose_; }
    [[nodiscard]] bool checkingPrograms() const { return checkingPrograms_; }
    [[nodiscard]] bool programsChecked() const { return programsChecked_; }

public slots:
    /// Starts a capture. `destinationFolder` is where the archive is written;
    /// the file name is derived from the machine name and the date.
    ///
    /// `domains` overrides what the profile would include - the interface lets
    /// the user tick these individually. `includeSecrets` is separate because
    /// it is the one choice that puts passwords on a removable drive, and it
    /// requires a passphrase.
    void start(const QString& profileId, const QString& destinationFolder, const QString& preset,
               const QString& passphrase, bool splitForFat32, const QString& label,
               const QStringList& domains, bool includeSecrets);

    /// The domains a profile includes, so the interface's tick boxes follow
    /// the profile the user just chose rather than contradicting it.
    [[nodiscard]] QStringList domainsForProfile(const QString& profileId);

    /// Looks for programs that are running and hold their data open.
    ///
    /// Asking the system what is installed means shelling out to a package
    /// manager, so this runs on a worker and answers through
    /// programsToClose.
    void checkForRunningPrograms(const QString& profileId, const QStringList& domains);

    /// Forgets the last answer, so leaving and re-entering the step asks again
    /// rather than showing what was true several minutes ago.
    void forgetRunningPrograms();

    /// Whether this system has a credential store worth offering.
    [[nodiscard]] bool secretsAvailable();

    /// Names the credential store, for the interface's explanation.
    [[nodiscard]] QString secretsStoreName();

    void cancel();

    /// Fills the shared report model with this run's notes. QML passes the
    /// model in, so one report view serves both capture and restore.
    void populateReport(ContinuityReportModel* model) const;

    /// Clears the finished state so the wizard can be run again.
    void reset();

    /// Uncompressed size of a profile, for the "will it fit" check.
    [[nodiscard]] quint64 estimateSize(const QString& profileId);

signals:
    void runningChanged();
    void programsToCloseChanged();
    void progressChanged();
    void finishedChanged();
    void reportReady();

private:
    void handleProgress(const core::ProgressUpdate& update);
    void handleFinished();
    void handleProgramCheckFinished();

    /// What a profile plus the user's tick boxes actually add up to. Shared by
    /// the capture and the pre-flight check so the check cannot be answering a
    /// different question from the one the capture will ask.
    [[nodiscard]] static core::CaptureSelection selectionFor(const QString& profileId,
                                                             const QStringList& domains,
                                                             bool includeSecrets);

    std::unique_ptr<platform::PlatformService> platform_;
    std::unique_ptr<core::ExportService> service_;
    core::CancelToken cancelToken_;

    QFutureWatcher<core::ExportReport> watcher_;
    QFutureWatcher<QList<platform::RunningApp>> programWatcher_;
    QStringList programsToClose_;
    bool checkingPrograms_ = false;
    bool programsChecked_ = false;
    core::ProgressUpdate progress_;
    core::ExportReport report_;
    qint64 startedAtMs_ = 0;
    bool running_ = false;
    bool finished_ = false;
};

}  // namespace transmit::app
