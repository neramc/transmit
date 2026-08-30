#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <memory>

#include "app/models/AppCatalogModel.h"
#include "app/models/CaptureFolderModel.h"
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

    /// The archive is finished and it went to a drive that can be taken away.
    Q_PROPERTY(bool canEject READ canEject NOTIFY ejectChanged)
    Q_PROPERTY(bool ejected READ ejected NOTIFY ejectChanged)
    Q_PROPERTY(QString ejectMessage READ ejectMessage NOTIFY ejectChanged)

    /// How the archive will be put together, in one line.
    Q_PROPERTY(QString packagingSummary READ packagingSummary NOTIFY packagingChanged)

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

    /// What a profile plus the user's choices actually add up to.
    ///
    /// Shared by the capture and the pre-flight check, so the check cannot be
    /// answering a different question from the one the capture will ask - and
    /// public because "what would this take" is a fair question to ask of the
    /// controller, not a hole opened for a test.
    [[nodiscard]] core::CaptureSelection selectionFor(const QString& profileId,
                                                      const QStringList& domains,
                                                      bool includeSecrets) const;

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

    [[nodiscard]] QString packagingSummary() const;

    [[nodiscard]] bool canEject() const;
    [[nodiscard]] bool ejected() const { return ejected_; }
    [[nodiscard]] QString ejectMessage() const { return ejectMessage_; }

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
    /// Unmounts the drive the archive went to, so it can be pulled out.
    ///
    /// The last step of carrying an archive to a stick is taking the stick
    /// away, and doing that while the system still has writes outstanding is
    /// how a capture that reported success arrives empty.
    void ejectDestination();

    /// The settings behind the "how it is packed" controls.
    ///
    /// Every one of them has a default that suits the drive it is going to,
    /// and every one of them is worth being able to change: a block size that
    /// suits a hard disk wastes memory on a small machine, a sync interval
    /// that suits a stick slows an internal disk down for nothing, and the
    /// number of workers is the difference between a laptop that stays usable
    /// during a capture and one that does not.
    ///
    /// Zero means "as Transmit would choose", which is what they start at.
    /// Sizes are in bytes and arrive as doubles because that is what QML has.
    void setPackaging(double solidBlockSize, int workerCount, double syncIntervalBytes,
                      bool verifyAfterWriting, bool keepJournal);

    /// Back to what Transmit would choose on its own.
    void clearPackaging();

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

    /// Narrows the capture to the folders of your own that were ticked.
    ///
    /// A profile decides which folders a capture would take; this takes some
    /// of them away. It only ever removes: a folder the profile never asked
    /// for is not added by ticking it, because the profile is what says
    /// whether that domain is being captured at all.
    void chooseFolders(CaptureFolderModel* model);

    /// Back to whatever the profile says, which is the default.
    void captureEveryFolder();

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
    void packagingChanged();
    void ejectChanged();

private:
    void handleProgress(const core::ProgressUpdate& update);
    void handleFinished();
    void handleProgramCheckFinished();

    std::unique_ptr<platform::PlatformService> platform_;
    std::unique_ptr<core::ExportService> service_;
    core::CancelToken cancelToken_;

    QFutureWatcher<core::ExportReport> watcher_;
    QFutureWatcher<QList<platform::RunningApp>> programWatcher_;
    QStringList programsToClose_;
    bool checkingPrograms_ = false;
    bool programsChecked_ = false;
    core::ScopeRule scope_;

    /// Set when the user has narrowed the folders by hand. Empty means the
    /// profile's own list stands.
    QList<core::CaptureRoot> chosenFolders_;
    bool foldersNarrowed_ = false;
    QList<core::AppSelection> apps_;
    core::AppSelectionMode appMode_ = core::AppSelectionMode::All;
    int appsChosen_ = 0;
    int appsOffered_ = 0;

    /// Where the archive went, and how taking the drive away went. Kept from
    /// the start of the capture: by the time the offer is made, the wizard has
    /// moved on and no longer has it to hand.
    QString destinationFolder_;
    bool ejected_ = false;
    QString ejectMessage_;

    /// The advanced packaging settings. Zero is "let Transmit decide", which
    /// is not the same as any particular value and has to stay distinguishable
    /// from one: a sync interval of zero means "only at the end", and the
    /// default on a removable drive is 32 MiB.
    core::PackagingOptions packaging_;

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
