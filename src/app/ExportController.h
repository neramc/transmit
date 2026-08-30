#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <memory>

#include "app/models/AppCatalogModel.h"
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
    Q_PROPERTY(bool incomplete READ incomplete NOTIFY finishedChanged)
    Q_PROPERTY(QString incompleteText READ incompleteText NOTIFY finishedChanged)
    Q_PROPERTY(bool verified READ verified NOTIFY finishedChanged)
    Q_PROPERTY(QString verificationText READ verificationText NOTIFY finishedChanged)
    /// An unfinished capture was found where the archive would go, and can be
    /// carried on with instead of started again.
    Q_PROPERTY(bool canCarryOn READ canCarryOn NOTIFY carryOnChanged)
    Q_PROPERTY(QString carryOnText READ carryOnText NOTIFY carryOnChanged)
    Q_PROPERTY(bool carryingOn READ carryingOn NOTIFY carryOnChanged)

    Q_PROPERTY(QString scopeSummary READ scopeSummary NOTIFY scopeChanged)
    Q_PROPERTY(QString applicationSummary READ applicationSummary NOTIFY scopeChanged)

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

    /// The capture worked, but some of what was selected could not even be
    /// looked at. Shown as a warning next to the success, because somebody
    /// about to wipe this machine has to know before they do.
    [[nodiscard]] bool incomplete() const { return report_.incomplete; }
    [[nodiscard]] QString incompleteText() const;
    [[nodiscard]] QStringList archiveParts() const { return report_.archiveParts; }

    /// The archive was read back off the drive and every file matched.
    [[nodiscard]] bool verified() const { return report_.verified; }

    /// What the read-back found, in a sentence. Empty when none was asked for.
    [[nodiscard]] QString verificationText() const;

    [[nodiscard]] const core::ExportReport& report() const { return report_; }

    /// Programs the user would do well to close first, by the name they would
    /// recognise. Empty until a check has run.
    [[nodiscard]] QStringList programsToClose() const { return programsToClose_; }
    [[nodiscard]] bool checkingPrograms() const { return checkingPrograms_; }
    [[nodiscard]] bool programsChecked() const { return programsChecked_; }

    /// What the limits on the files add up to, in one line. "Everything" when
    /// nothing has been narrowed, which is the state a user should be able to
    /// recognise without reading three controls.
    [[nodiscard]] QString scopeSummary() const;

    /// The same for the per-application choice.
    [[nodiscard]] QString applicationSummary() const;

    [[nodiscard]] bool canCarryOn() const { return !interruptedArchive_.isEmpty(); }
    [[nodiscard]] QString carryOnText() const { return carryOnText_; }
    [[nodiscard]] bool carryingOn() const { return carryingOn_; }

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

    /// Takes the per-application answer from the model the interface showed.
    ///
    /// Read here rather than held by the model so that the capture and the
    /// "what should I close first" check are asking the same question - the
    /// model belongs to a page that may already have been left behind.
    /// Looks in `destinationFolder` for a capture a previous run left
    /// unfinished. Cheap enough to call whenever the folder changes: it reads
    /// the record beside each archive, not the archives themselves.
    void lookForInterruptedCapture(const QString& destinationFolder);

    /// Carry on with what was found rather than starting again. The next
    /// start() writes into that archive; the settings still have to match, and
    /// the capture says so plainly if they do not.
    void carryOn();

    /// Start again, leaving the unfinished capture to be overwritten.
    void startFresh();

    void chooseApplications(AppCatalogModel* model);

    /// Back to carrying every application's data, which is the default and
    /// what a profile on its own means.
    void carryEveryApplication();

    /// Limits which files are taken.
    ///
    /// `maximumFileSize` is in bytes and 0 means no limit; `modifiedWithinDays`
    /// is 0 for any age; `excludedExtensions` is a free-text list such as
    /// "iso, vmdk dmg" - punctuation and dots are ignored, because a person
    /// typing a list of file types should not have to guess the separator.
    void setScope(double maximumFileSize, int modifiedWithinDays,
                  const QString& excludedExtensions);

    /// Everything again: no size limit, no age limit, no excluded types.
    void clearScope();

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
    void scopeChanged();
    void carryOnChanged();

private:
    void handleProgress(const core::ProgressUpdate& update);
    void handleFinished();
    void handleProgramCheckFinished();

    /// What a profile plus the user's tick boxes actually add up to. Shared by
    /// the capture and the pre-flight check so the check cannot be answering a
    /// different question from the one the capture will ask.
    [[nodiscard]] core::CaptureSelection selectionFor(const QString& profileId,
                                                      const QStringList& domains,
                                                      bool includeSecrets) const;

    std::unique_ptr<platform::PlatformService> platform_;
    std::unique_ptr<core::ExportService> service_;
    core::CancelToken cancelToken_;

    QFutureWatcher<core::ExportReport> watcher_;
    QFutureWatcher<QList<platform::RunningApp>> programWatcher_;
    QStringList programsToClose_;
    bool checkingPrograms_ = false;
    bool programsChecked_ = false;
    core::ScopeRule scope_;
    QList<core::AppSelection> apps_;
    core::AppSelectionMode appMode_ = core::AppSelectionMode::All;
    int appsChosen_ = 0;
    int appsOffered_ = 0;

    /// The unfinished capture found where the archive would go, and whether
    /// the user chose to carry on with it rather than start again.
    QString interruptedArchive_;
    QString carryOnText_;
    bool carryingOn_ = false;
    core::ProgressUpdate progress_;
    core::ExportReport report_;
    qint64 startedAtMs_ = 0;
    bool running_ = false;
    bool finished_ = false;
};

}  // namespace transmit::app
