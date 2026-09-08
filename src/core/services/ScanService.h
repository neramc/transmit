#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QString>

#include <atomic>
#include <functional>

#include "core/continuity/ContinuityTypes.h"
#include "format/Manifest.h"
#include "platform/PlatformService.h"

namespace transmit::core {

/// One filesystem object found by a scan, already carrying everything the
/// manifest needs so the capture pass does not have to stat it again.
struct ScannedItem {
    QString absolutePath;
    format::TokenizedPath tokenPath;
    format::EntryType type = format::EntryType::File;
    DomainId domain = DomainId::UserData;
    QString appId;
    quint64 size = 0;
    qint64 modifiedUnixNs = 0;
    qint64 createdUnixNs = 0;
    format::PosixMetadata posix;
    format::WindowsMetadata windows;
    QString symlinkTarget;

    /// The tags the filesystem keeps beside the file - a colour label, a
    /// Finder tag, a comment. Empty for almost every file.
    std::vector<format::ExtendedAttribute> extendedAttributes;

    /// Which file on the source machine this name points at, set only when
    /// the file has more than one name. Both zero otherwise, which is nearly
    /// every file.
    ///
    /// Two items with the same pair are two names for one file - a hard link -
    /// and the restore is meant to make them two names again rather than two
    /// files. The volume has to be part of it: inode numbers are only unique
    /// within a filesystem, and a capture that spans two of them would
    /// otherwise link together files that have nothing to do with each other.
    quint64 sharedVolume = 0;
    quint64 sharedFile = 0;

    /// Set when the scan could not read the item; it still appears in the
    /// report so nothing disappears silently.
    QString problem;
};

/// Whether this file's bytes are somewhere other than this machine, so that
/// reading it would fetch them over the network.
///
/// Two systems, two pieces of evidence. Windows sets bits in the attribute
/// word - OneDrive's "Files On-Demand" leaves the name, the size and the
/// modification time on disk and nothing else. macOS has no such bit: iCloud
/// Drive replaces an evicted `report.pdf` with a stub called
/// `.report.pdf.icloud`, so the name is the only evidence a plain scan has.
///
/// The system is a parameter rather than a compiled-in fact, because the rules
/// differ per system and a rule that only runs on the machine that has it is a
/// rule nobody can test. It also keeps the macOS name rule off Linux, where a
/// file somebody genuinely called ".notes.txt.icloud" is a file.
[[nodiscard]] bool storedOnlyInTheCloud(const QString& fileName,
                                        const format::WindowsMetadata& windows, OsFamily host);

struct ScanResult {
    QList<ScannedItem> items;
    quint64 totalBytes = 0;
    quint64 fileCount = 0;
    quint64 directoryCount = 0;
    quint64 symlinkCount = 0;
    quint64 skippedCount = 0;

    /// What the files kept online would have cost to download. Reported as a
    /// size rather than only a count, because "1,204 files" and "310 GB" lead
    /// to different decisions and only the second one is the reason to care.
    quint64 cloudOnlyBytes = 0;

    /// Folders the scan could not look inside. QDirIterator walks past those
    /// without a word, so without this list a capture that missed a whole
    /// subtree looks exactly like one that had nothing to find there.
    QStringList unreadableDirectories;

    QList<ContinuityNote> notes;

    /// How many files each reason accounted for, so the interface can say
    /// "412 excluded: 380 over the size limit, 22 too old, 10 unreadable"
    /// rather than only a number - which tells nobody whether to change
    /// anything.
    QHash<int, quint64> skippedByReason;

    /// True when something the selection asked for could not even be looked
    /// at. The capture still runs - most of it is fine - but nothing may
    /// describe the result as complete.
    [[nodiscard]] bool incomplete() const noexcept { return !unreadableDirectories.isEmpty(); }
};

/// Cooperative cancellation shared between the UI and a running job.
class CancelToken {
public:
    void cancel() noexcept { cancelled_.store(true, std::memory_order_relaxed); }
    void reset() noexcept { cancelled_.store(false, std::memory_order_relaxed); }
    [[nodiscard]] bool isCancelled() const noexcept {
        return cancelled_.load(std::memory_order_relaxed);
    }

private:
    std::atomic_bool cancelled_{false};
};

/// Compiles the wildcard exclusion patterns once and matches relative paths
/// against them. Patterns use "**" for "any number of directories".
class ExcludeMatcher {
public:
    explicit ExcludeMatcher(const QStringList& patterns = {});

    void add(const QStringList& patterns);
    [[nodiscard]] bool matches(const QString& relativePath) const;
    [[nodiscard]] bool isEmpty() const noexcept { return patterns_.isEmpty(); }

private:
    QList<QRegularExpression> patterns_;
};

/// Walks a CaptureSelection and produces the item list the capture works from.
/// Kept separate from the capture so the UI can show sizes and counts, and let
/// the user adjust the selection, before anything is written.
class ScanService {
public:
    using ProgressCallback = std::function<void(const ProgressUpdate&)>;

    explicit ScanService(const platform::PlatformService& platformService);

    [[nodiscard]] ScanResult scan(const CaptureSelection& selection, CancelToken& cancelToken,
                                  const ProgressCallback& progress = {}) const;

private:
    void scanRoot(const CaptureRoot& root, const CaptureSelection& selection,
                  const ExcludeMatcher& globalExcludes, ScanResult& result,
                  CancelToken& cancelToken, const ProgressCallback& progress) const;

    // Only the folder table is needed after construction; holding the whole
    // service would tie every scan to the object that created it.
    format::PathTokenMap tokens_;

    /// Which system's rules apply to what is on this disk. Taken from the
    /// platform service rather than from a compiled-in macro, so the macOS
    /// rules can be run against a fixture anywhere.
    OsFamily host_ = OsFamily::Unknown;
};

}  // namespace transmit::core
