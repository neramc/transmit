#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "format/Bytes.h"
#include "format/FileIo.h"
#include "format/JournalFile.h"
#include "format/Manifest.h"
#include "format/Result.h"
#include "format/VolumeSplitter.h"

namespace transmit::format {

/// An append-only account of where a restore has actually put things.
///
/// A restore writes into the places a person keeps their life, one file at a
/// time, and can be interrupted at any of them. Running it again is safe -
/// a file that is already byte for byte what the archive holds is recognised
/// and left alone - but "safe" is doing a great deal of work there: it means
/// re-reading and re-hashing every file that was already done, and it means
/// the second run rediscovering, rather than remembering, every decision the
/// first one made.
///
/// The second part is where it stops being merely slow. When two files in the
/// archive collide on this system, "keep both" saves the second one alongside
/// the first under a name it invents. A resumed run that does not know which
/// name was invented invents a different one, and the user is left with two
/// copies of a file they have one of.
///
/// So the restore keeps this record: for every item it settles, where the item
/// came from in the archive and where it actually landed. A resumed run reads
/// it, skips what is done, and reuses the names the first run chose.
///
/// ### Written behind the data, not in front of it
///
/// The capture journal is synced *before* the bytes it describes, so that its
/// own tail is the only thing a power cut can tear. This one is the other way
/// round: a record is appended only once the file it names is on disk.
///
/// The reason is which mistake each can afford. For a capture, a journal that
/// has fallen behind the archive means resuming from an offset that is merely
/// early - the extra bytes are rewritten. For a restore, a journal that has run
/// *ahead* of the disk means skipping a file that was never written, and the
/// user is quietly missing something they asked for. Falling behind costs a
/// file being restored twice, which costs nothing at all. So it falls behind.
enum class RestoreRecordKind : std::uint8_t {
    SessionStart = 1,
    ItemPlaced = 2,
    SessionComplete = 3,
};

/// What happened to one item.
enum class RestoreOutcome : std::uint8_t {
    /// On disk, at `target`. A resumed run leaves it alone.
    Written = 1,

    /// Deliberately not written: a conflict policy said so, or the system
    /// cannot represent it. A resumed run makes the same decision again
    /// rather than trusting this one, because the decision depended on what
    /// was on disk and that may have changed - but the record is kept so the
    /// report of an interrupted run is still complete.
    Skipped = 2,

    /// Tried and could not be done. Recorded for the same reason.
    Failed = 3,
};

/// What the restore was, so a resume can refuse one that no longer matches.
struct RestoreFingerprint {
    /// The archive being restored. A journal from a different archive
    /// describes different files under the same names.
    ArchiveUuid archiveUuid{};

    /// Empty when restoring into the real known folders; otherwise the folder
    /// everything was redirected into. Resuming a restore that went to a
    /// scratch directory into somebody's home is not a resume.
    std::string destination;

    std::string hostName;
    std::string userName;

    /// Covers the conflict policy, the emulated OS and which domains were
    /// asked for. All three change where files land, so a run with different
    /// ones is a different restore that happens to share an archive.
    std::uint64_t optionsDigest = 0;

    /// Where the undo point was saved. Kept here because a restore that is
    /// interrupted loses the in-memory report that would otherwise be the
    /// only place this path was written down - and that is exactly the run
    /// somebody most wants to undo.
    std::string rollbackArchivePath;

    friend bool operator==(const RestoreFingerprint& a, const RestoreFingerprint& b) noexcept {
        return a.archiveUuid == b.archiveUuid && a.destination == b.destination &&
               a.hostName == b.hostName && a.userName == b.userName &&
               a.optionsDigest == b.optionsDigest;
    }
};

/// One item the restore settled.
struct RestorePlacement {
    /// The item as the archive records it, e.g. "{DOCUMENTS}/notes.txt". This
    /// is the key: it is what a resumed run has in hand before it has worked
    /// out where the item would go.
    std::string source;

    /// Where it actually landed - after name sanitising, after application
    /// state was relocated, after "keep both" invented a name. Reusing this is
    /// the whole point of the record.
    std::string target;

    RestoreOutcome outcome = RestoreOutcome::Written;
};

/// What a journal held when it was read back.
struct RestoreJournalContents {
    RestoreFingerprint fingerprint;
    std::vector<RestorePlacement> placements;

    bool complete = false;

    /// Records at the end were incomplete or failed their checksum and were
    /// discarded. For this journal that is not even a loss: the files they
    /// described are on disk and will be recognised as already right.
    bool tailDiscarded = false;

    std::uint64_t validBytes = kJournalHeaderSize;

    /// How many items were actually written, as against skipped or failed.
    [[nodiscard]] std::uint64_t writtenCount() const noexcept;
};

/// Writes the journal. One instance per restore.
class RestoreJournal {
public:
    static constexpr std::string_view kSuffix = ".journal";

    /// Journals live beside the undo point, in the destination's own
    /// ".transmit" folder, named for the archive: two restores of two archives
    /// into one place must not read each other's records.
    static std::filesystem::path pathFor(const std::filesystem::path& stateDirectory,
                                         const ArchiveUuid& archive);

    RestoreJournal() = default;
    ~RestoreJournal();

    RestoreJournal(const RestoreJournal&) = delete;
    RestoreJournal& operator=(const RestoreJournal&) = delete;

    /// Starts a journal, replacing any that was there.
    static Result<std::unique_ptr<RestoreJournal>> begin(const std::filesystem::path& path,
                                                         const RestoreFingerprint& fingerprint);

    /// Reopens a journal to append to, truncated to the records that were read
    /// back, so a discarded torn tail does not come back.
    static Result<std::unique_ptr<RestoreJournal>> reopen(const std::filesystem::path& path,
                                                          std::uint64_t keepBytes);

    /// Records an item that is settled. Called after the write, never before.
    Status recordPlacement(const RestorePlacement& placement);

    /// Marks the restore finished.
    Status recordComplete();

    /// Pushes what has been written to the device.
    ///
    /// Not called per item on purpose. A restore settles thousands of files
    /// and a sync is milliseconds, so syncing each one would cost more than
    /// the restore; and because this journal is allowed to fall behind the
    /// disk, an unsynced tail costs only that those files are done again.
    Status sync();

    Status close();

    static Status discard(const std::filesystem::path& path);

    [[nodiscard]] std::uint64_t bytesWritten() const noexcept { return bytesWritten_; }

private:
    FileStream stream_;
    std::uint64_t bytesWritten_ = 0;
};

/// Reads a journal back, discarding any torn tail.
///
/// Missing file is reported as `NotFound`, which for this journal means
/// "nothing was interrupted here" rather than an error.
Result<RestoreJournalContents> readRestoreJournal(const std::filesystem::path& path);

}  // namespace transmit::format
