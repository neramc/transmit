#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <QList>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

#include "format/Container.h"
#include "format/Manifest.h"
#include "format/PathToken.h"
#include "format/codec/Codec.h"

namespace transmit::core {

using format::DomainId;
using format::OsFamily;
using format::PathTokenId;

/// How faithfully one item survived the move to another operating system. The
/// report is built from these, so a user can see at a glance what came across
/// untouched and what still needs their attention.
enum class ContinuityGrade {
    Full,        ///< byte-for-byte identical on the new machine
    Adapted,     ///< carried across with a translation (paths rewritten, a setting approximated)
    Manual,      ///< possible, but the user has to do something (run a script, click a setting)
    Impossible,  ///< cannot cross this OS boundary at all
};

QString continuityGradeName(ContinuityGrade grade);
QString continuityGradeDescription(ContinuityGrade grade);

/// One line in the continuity report.
struct ContinuityNote {
    ContinuityGrade grade = ContinuityGrade::Full;
    DomainId domain = DomainId::Unknown;
    QString subject;  ///< what this is about: a path, a setting key, an app name
    QString detail;   ///< why it got this grade, in words the user can act on
};

/// Why a file was left out, so the interface can say "412 excluded: 380 over
/// the size limit, 22 too old, 10 unreadable" rather than only a number.
enum class SkipReason {
    TooLarge,
    TooSmall,
    WrongExtension,
    TooOld,
    TooNew,
    Excluded,
    Hidden,
    Unreadable,
};

QString skipReasonName(SkipReason reason);

/// Which files, out of everything a folder holds, are wanted.
///
/// Every field here was previously either fixed or scattered: the size limit
/// and the exclude patterns lived on the selection with no way to reach them
/// from the interface at all, and there was no way to say "only what I have
/// touched this year" or "not the video files" - which is the difference
/// between a capture that fits on the stick and one that does not.
///
/// A default-constructed rule takes everything.
struct ScopeRule {
    /// 0 means no limit. Both ends, because "nothing over 2 GB" and "nothing
    /// under 1 KB" are both things people want and neither implies the other.
    quint64 maximumFileSize = 0;
    quint64 minimumFileSize = 0;

    /// Extensions without the dot, lowercase. An include list, when it is not
    /// empty, is exclusive: nothing else comes. Matched as a set rather than
    /// with a pattern because this runs once per file.
    QSet<QString> includeExtensions;
    QSet<QString> excludeExtensions;

    /// Invalid means no bound. Compared against the modification time.
    QDateTime modifiedSince;
    QDateTime modifiedBefore;

    /// Wildcard patterns matched against the path relative to the root.
    QStringList excludePatterns;

    /// Symbolic links are recorded as links by default; following them risks
    /// pulling in the same tree twice or escaping the selection entirely.
    bool followSymlinks = false;

    /// Hidden files are carried by default: on every system Transmit runs on,
    /// that is where the settings are.
    bool includeHidden = true;

    /// True when this rule would accept everything, which lets the scan skip
    /// the per-file work entirely.
    [[nodiscard]] bool isUnrestricted() const;

    /// Why this file is not wanted, or nothing when it is. Given the size
    /// separately because the scan has already read it and a second stat per
    /// file is measurable over a home directory.
    [[nodiscard]] std::optional<SkipReason> reject(quint64 size, const QFileInfo& info) const;

    /// Excludes that pay for themselves on almost every machine.
    static QStringList defaultExcludes();
};

/// One place to capture from. A selection is a list of these rather than a list
/// of absolute paths, so the same profile means the right thing on every OS.
struct CaptureRoot {
    PathTokenId token = PathTokenId::Home;
    QString relative;  ///< empty captures the whole known folder
    DomainId domain = DomainId::UserData;
    QString appId;  ///< set for application state, so the report can group by app

    /// Which of the application's state roots this is, for the per-application
    /// choice and for the move rules that name a root.
    QString stateRootId;

    bool recursive = true;
    QStringList excludePatterns;  ///< wildcard patterns matched against the relative path

    /// Narrower than the selection's own rule, for this root alone. Left
    /// unrestricted, the selection's applies.
    ScopeRule scope;

    /// True for the broad profile roots - the whole of {APPCONFIG}, say - that
    /// exist to catch applications with no recipe. A file inside one of those
    /// belongs to whichever specific root also covers it, if any.
    bool isFallback = false;

    /// How specific this root is. Higher wins when two roots cover the same
    /// file, which decides which application the file is credited to.
    [[nodiscard]] int specificity() const;
};

/// Whether an application's own data comes along.
enum class AppSelectionMode {
    All,       ///< every application whose data can travel
    None,      ///< record what is installed, carry none of it
    Explicit,  ///< only the ones named
};

/// One application's answer.
struct AppSelection {
    QString appId;

    /// Carry this application's settings and data.
    bool captureState = true;

    /// Note that it was installed, so the restore can offer to install it
    /// again. Independent of the above: somebody may want the list without
    /// the data, and the list costs nothing.
    bool recordForReinstall = true;

    /// Empty means every root the recipe has. Named roots let somebody take a
    /// browser's settings without its site storage.
    QStringList stateRootIds;

    /// Narrower than the selection's own rule, for this application alone.
    ScopeRule scope;
};

/// What a capture should collect.
struct CaptureSelection {
    QList<CaptureRoot> roots;
    QSet<int> domains{static_cast<int>(DomainId::UserData)};

    /// Applied on top of every root unless the root has a narrower one.
    ScopeRule scope;

    AppSelectionMode appMode = AppSelectionMode::All;

    /// Consulted when `appMode` is Explicit, and as an override otherwise: an
    /// entry here always wins over the mode.
    QList<AppSelection> apps;

    [[nodiscard]] bool includes(DomainId domain) const {
        return domains.contains(static_cast<int>(domain));
    }

    /// The answer for one application, taking the mode and any explicit entry
    /// into account.
    [[nodiscard]] AppSelection answerFor(const QString& appId) const;

    /// Whether this application's state should be captured at all.
    [[nodiscard]] bool capturesStateOf(const QString& appId) const;

    /// Excludes that pay for themselves on almost every machine.
    static QStringList defaultExcludes();
};

/// How the archive itself is built, as distinct from what goes in it.
///
/// Separated so it can be written down and reproduced: the same selection
/// packed with a different block size is the same capture, and somebody
/// comparing two runs needs to see which of the two things changed.
struct PackagingOptions {
    format::CompressionPreset preset = format::CompressionPreset::Maximum;

    /// 0 writes a single file. The UI sets this from the target volume's
    /// filesystem, so a FAT32 stick gets split automatically.
    quint64 partSize = 0;

    quint64 solidBlockSize = format::kDefaultSolidBlockSize;

    /// 0 lets the pipeline choose from the machine and the preset.
    int workerCount = 0;

    /// How often the payload is pushed to the device while writing. 0 leaves
    /// it to the destination: removable media get 32 MiB, fixed disks nothing.
    quint64 syncIntervalBytes = 0;

    /// Read the archive back and check every file against its recorded hash,
    /// as soon as it is written and before the drive is unplugged.
    bool verifyAfterWriting = true;
};

struct ExportRequest {
    QString destinationPath;  ///< base archive path, without any part suffix
    QString label;            ///< free text shown when the archive is inspected
    CaptureSelection selection;
    PackagingOptions packaging;

    /// Required when the selection includes credentials.
    QString passphrase;
};

/// Which part of a run an update comes from.
///
/// `ProgressUpdate::stage` says the same thing in words, but those words are
/// translated and written for a person to read. A caller that needs to *act*
/// on the phase - a view that shows a different layout while data is moving,
/// a test that has to wait until the archive exists - reads this instead of
/// matching on prose.
enum class ProgressPhase {
    Preparing,     ///< Before anything is read or written.
    Scanning,      ///< Working out what the run will touch.
    Verifying,     ///< Checking data that is already there.
    Transferring,  ///< Moving the actual contents.
    Finishing      ///< Everything after the last item.
};

/// Emitted while a capture or restore runs. Throttled by the job so the UI
/// thread is not flooded.
struct ProgressUpdate {
    quint64 filesDone = 0;
    quint64 filesTotal = 0;
    quint64 bytesDone = 0;
    quint64 bytesTotal = 0;
    quint64 bytesStored = 0;
    QString currentItem;
    QString stage;
    ProgressPhase phase = ProgressPhase::Preparing;
};

struct ExportReport {
    bool succeeded = false;
    QString errorMessage;

    QStringList archiveParts;
    QString archiveId;
    quint64 fileCount = 0;
    quint64 directoryCount = 0;
    quint64 symlinkCount = 0;
    quint64 rawBytes = 0;
    quint64 storedBytes = 0;
    quint64 deduplicatedBytes = 0;
    qint64 elapsedMilliseconds = 0;
    bool encrypted = false;

    /// Something the selection asked for could not be looked at - almost
    /// always a folder belonging to another account. The archive is sound and
    /// everything in it is real; it is just not all of what was asked for, and
    /// the difference matters to somebody who is about to wipe the machine.
    bool incomplete = false;
    QStringList unreadablePaths;

    QList<ContinuityNote> notes;

    /// storedBytes as a fraction of rawBytes, or 1.0 when nothing was stored.
    [[nodiscard]] double compressionRatio() const;
};

/// What to do when a restored file already exists on the target.
enum class ConflictPolicy {
    Skip,
    Overwrite,
    KeepBoth,   ///< write alongside with a "~1" suffix
    NewerWins,  ///< compare modification times
};

QString conflictPolicyName(ConflictPolicy policy);

struct ImportRequest {
    QString archivePath;
    QString passphrase;

    /// Empty restores every domain the archive holds.
    QSet<int> domains;

    /// Restore into this directory instead of the real known folders. Used by
    /// the dry run and by "restore to a folder I choose".
    QString destinationOverride;

    ConflictPolicy conflictPolicy = ConflictPolicy::KeepBoth;

    /// Reports what would happen without writing anything.
    bool dryRun = false;

    /// Pretend to be another OS. This is how the cross-platform translation is
    /// exercised on a single machine, in tests and in the CLI.
    OsFamily emulateOs = OsFamily::Unknown;

    /// Save the previous contents of everything about to be overwritten, so the
    /// restore can be undone.
    bool createRollback = true;

    /// Verify every block's hash before writing anything.
    bool verifyFirst = false;

    /// Push each restored file to the device before its name appears, and the
    /// folders once at the end. Without it a power cut part way through a
    /// restore can leave files that exist but hold nothing - the one outcome
    /// worse than a file that was never written. Costs one device flush per
    /// file, so a dry run into scratch space turns it off.
    bool durableWrites = true;
};

struct RestoredItem {
    QString sourcePath;  ///< as recorded in the archive, e.g. {DOCUMENTS}/a.txt
    QString targetPath;  ///< where it landed on this machine
    ContinuityGrade grade = ContinuityGrade::Full;
    bool skipped = false;
    QString note;
};

struct ImportReport {
    bool succeeded = false;
    QString errorMessage;

    quint64 filesRestored = 0;

    /// Left alone on purpose: a conflict policy said so, or the system
    /// cannot represent the item at all. Not a fault.
    quint64 filesSkipped = 0;

    /// Tried and could not be done: unreadable from the archive,
    /// unwritable to disk, a folder that could not be created. Every one
    /// of these is a file the user expected and has not got.
    quint64 filesFailed = 0;

    quint64 bytesWritten = 0;
    qint64 elapsedMilliseconds = 0;

    /// True when some files were restored and some failed. The run is
    /// neither a success to report nor an error to hand back, and the
    /// difference matters most to the undo point: a half-restored
    /// machine is exactly when somebody wants to put it back.
    [[nodiscard]] bool partial() const noexcept { return filesFailed > 0 && filesRestored > 0; }

    QString rollbackArchivePath;

    /// Files whose contents were repointed at this machine's folders. Each has
    /// its pre-rewrite version kept beside it, so this is also the list of
    /// backups to clear away once the restore is being kept.
    QStringList rewrittenFiles;

    /// Where the generated install script was written, if any. Transmit never
    /// runs it: installing software is the user's decision and their password.
    QString installScriptPath;
    int programsToInstall = 0;
    int programsNeedingManualInstall = 0;
    QList<ContinuityNote> notes;
    QList<RestoredItem> items;

    /// Paths that had to be renamed for the target filesystem.
    QList<QPair<QString, QString>> renames;

    [[nodiscard]] int countOf(ContinuityGrade grade) const;
};

}  // namespace transmit::core

Q_DECLARE_METATYPE(transmit::core::ProgressUpdate)
Q_DECLARE_METATYPE(transmit::core::ExportReport)
Q_DECLARE_METATYPE(transmit::core::ImportReport)
