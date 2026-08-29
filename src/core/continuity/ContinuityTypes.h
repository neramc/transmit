#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QStringList>

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

/// One place to capture from. A selection is a list of these rather than a list
/// of absolute paths, so the same profile means the right thing on every OS.
struct CaptureRoot {
    PathTokenId token = PathTokenId::Home;
    QString relative;  ///< empty captures the whole known folder
    DomainId domain = DomainId::UserData;
    QString appId;  ///< set for application state, so the report can group by app
    bool recursive = true;
    QStringList excludePatterns;  ///< wildcard patterns matched against the relative path
};

/// What a capture should collect.
struct CaptureSelection {
    QList<CaptureRoot> roots;
    QSet<int> domains{static_cast<int>(DomainId::UserData)};

    /// Applied on top of every root. Defaults cover caches, build output and
    /// other content that is large, regenerable and not worth carrying.
    QStringList globalExcludePatterns;

    /// Symbolic links are recorded as links by default; following them risks
    /// pulling in the same tree twice or escaping the selection entirely.
    bool followSymlinks = false;

    /// 0 means no limit. Used by the UI's "skip files larger than" control.
    quint64 maximumFileSize = 0;

    [[nodiscard]] bool includes(DomainId domain) const {
        return domains.contains(static_cast<int>(domain));
    }

    /// Excludes that pay for themselves on almost every machine.
    static QStringList defaultExcludes();
};

struct ExportRequest {
    QString destinationPath;  ///< base archive path, without any part suffix
    QString label;            ///< free text shown when the archive is inspected
    CaptureSelection selection;
    format::CompressionPreset preset = format::CompressionPreset::Maximum;

    /// 0 writes a single file. The UI sets this from the target volume's
    /// filesystem, so a FAT32 stick gets split automatically.
    quint64 partSize = 0;

    /// Required when the selection includes credentials.
    QString passphrase;

    quint64 solidBlockSize = format::kDefaultSolidBlockSize;
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
