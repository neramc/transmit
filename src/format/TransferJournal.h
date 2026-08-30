#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "format/Bytes.h"
#include "format/FileIo.h"
#include "format/Manifest.h"
#include "format/Result.h"
#include "format/VolumeSplitter.h"

namespace transmit::format {

/// An append-only account of what a capture has actually got onto the drive.
///
/// A capture onto a USB stick can take twenty minutes, and the archive is only
/// readable at the very end: the manifest and the footer are written by
/// `finish()`, so a run interrupted at nineteen minutes leaves a file that
/// holds nearly all the data and can be opened by nothing. Without a record
/// kept as it goes, the only recovery is to start again.
///
/// The journal is that record. Every block the writer commits and every
/// manifest entry that lands in one is appended to `name.txa.journal` before
/// the capture moves on, so a later run can pick the archive up where the
/// interrupted one stopped: truncate the parts back to the last block the
/// journal saw, reload the entries and the deduplication table, and carry on
/// with the files that were never reached.
///
/// It is deleted when the capture finishes. A journal beside an archive means
/// the archive is unfinished.
///
/// ### Why not just re-read the archive
///
/// Because an interrupted archive cannot be read. There is no manifest yet,
/// the footer is absent, and the part headers still say the set is unfinished.
/// Walking the block records that happen to be there would mean trusting a
/// tail that was being written when the power went - which is the one part of
/// the file that cannot be trusted. The journal is written and synced ahead of
/// the data it describes precisely so that its own tail is the only thing that
/// can be torn, and a torn tail is detectable and discardable.
enum class JournalRecordKind : std::uint8_t {
    SessionStart = 1,
    BlockWritten = 2,
    EntryPlaced = 3,
    SessionComplete = 4,
};

/// What the capture was, so a resume can refuse one that no longer matches.
///
/// Resuming into an archive whose source has changed underneath it would
/// produce a file that is internally consistent and describes a machine that
/// never existed: half of it from Tuesday, half from Thursday, and nothing
/// saying so. Every field here is compared before a single byte is reused.
struct JournalFingerprint {
    ArchiveUuid archiveUuid{};

    /// Where the archive is being written. A journal that was moved beside a
    /// different archive is not this archive's journal.
    std::string destination;

    std::string hostName;
    std::string userName;

    /// Covers the packaging: preset, part size, block size, whether it is
    /// encrypted, whether MD5s are recorded. Blocks already on the drive were
    /// compressed with these settings and cannot be mixed with others.
    std::uint64_t optionsDigest = 0;

    /// Covers the scan: every item's path, size and modification time, in the
    /// order the capture will walk them. This is the strict one - a single
    /// file changed since the interrupted run and the resume is refused,
    /// because the entries already in the journal describe the old state and
    /// there is no way to tell from here whether that matters.
    std::uint64_t sourceDigest = 0;

    friend bool operator==(const JournalFingerprint& a, const JournalFingerprint& b) noexcept {
        return a.archiveUuid == b.archiveUuid && a.destination == b.destination &&
               a.hostName == b.hostName && a.userName == b.userName &&
               a.optionsDigest == b.optionsDigest && a.sourceDigest == b.sourceDigest;
    }
};

/// One block that reached the drive.
///
/// Everything the manifest's own block directory needs, because that is what
/// this becomes: a resumed capture finishes with a directory made of the
/// blocks a previous run wrote and the ones this one added, and a reader
/// cannot tell which was which.
struct JournalBlock {
    std::uint32_t blockId = 0;

    /// Where the block's header starts in the logical stream.
    std::uint64_t streamOffset = 0;

    /// The logical length of the archive once this block was written. A
    /// resume truncates the parts to the last of these, which is the only
    /// offset known to be a record boundary rather than the middle of one.
    /// Kept alongside the offset rather than computed from it so the journal
    /// need not know how large a block header is.
    std::uint64_t logicalEnd = 0;

    std::uint64_t rawSize = 0;
    std::uint64_t storedSize = 0;
    CodecId codec = CodecId::Zstd;
    bool encrypted = false;
    Digest256 rawHash{};

    /// The same block as the manifest records it.
    [[nodiscard]] BlockRecord asBlockRecord() const {
        BlockRecord record;
        record.blockId = blockId;
        record.streamOffset = streamOffset;
        record.rawSize = rawSize;
        record.storedSize = storedSize;
        record.codec = codec;
        record.encrypted = encrypted;
        return record;
    }
};

/// What a journal held when it was read back.
struct JournalContents {
    JournalFingerprint fingerprint;
    std::vector<JournalBlock> blocks;
    std::vector<ManifestEntry> entries;

    /// The capture reached `finish()`. Nothing needs resuming; the archive is
    /// whole and the journal simply outlived its deletion.
    bool complete = false;

    /// Records at the end were incomplete or failed their checksum and were
    /// discarded. Expected after a power cut - the journal is written in front
    /// of the data, so its own tail is what gets torn - and not an error.
    bool tailDiscarded = false;

    /// Length of the records that survived, which is what the journal itself
    /// is truncated to before it is appended to again.
    std::uint64_t validBytes = 0;

    /// How much of the archive the surviving records account for.
    [[nodiscard]] std::uint64_t resumableLength() const noexcept {
        return blocks.empty() ? 0 : blocks.back().logicalEnd;
    }

    /// Drops everything the archive on the drive cannot actually back up.
    ///
    /// The journal is written and synced ahead of the data it describes, so it
    /// can outlive it: the record of a block reaches the drive and the block
    /// itself does not. Given how long the archive really is, this throws away
    /// the blocks that end past it, and then the entries that pointed into
    /// them - which is the whole reason it is one operation and not two.
    void keepOnlyWhatFitsIn(std::uint64_t archiveLength);
};

/// Writes the journal. One instance per capture; the file is opened once and
/// synced at each commit point.
class TransferJournal {
public:
    static constexpr std::string_view kSuffix = ".journal";

    /// "/media/usb/home.txa" -> "/media/usb/home.txa.journal".
    static std::filesystem::path pathFor(const std::filesystem::path& archive);

    TransferJournal() = default;
    ~TransferJournal();

    TransferJournal(const TransferJournal&) = delete;
    TransferJournal& operator=(const TransferJournal&) = delete;

    /// Starts a journal, replacing any that was there. Writes the fingerprint
    /// and syncs before returning, so an archive never gets ahead of the
    /// journal that describes it.
    static Result<std::unique_ptr<TransferJournal>> begin(const std::filesystem::path& archive,
                                                          const JournalFingerprint& fingerprint);

    /// Reopens a journal to append to, after its contents have been read and
    /// the archive truncated to match. `keepRecords` is how many records to
    /// keep - everything the read returned - and the file is truncated to
    /// exactly their length so a discarded torn tail does not come back.
    static Result<std::unique_ptr<TransferJournal>> reopen(const std::filesystem::path& archive,
                                                           std::uint64_t keepBytes);

    /// Records a block that is now on the drive. Called after the block's
    /// bytes are written, never before: a journal claiming a block the archive
    /// has not got would send a resume past the end of the data.
    Status recordBlock(const JournalBlock& block);

    /// Records an entry whose location is final. Only valid once the block it
    /// points into has been recorded.
    Status recordEntry(const ManifestEntry& entry);

    /// Marks the capture finished. After this the journal is only evidence,
    /// not a resume point.
    Status recordComplete();

    /// Pushes everything written so far to the device. Called at block
    /// boundaries rather than per entry: an entry record is a few hundred
    /// bytes and a sync on a stick is milliseconds, so syncing each one would
    /// cost more than the capture.
    Status sync();

    Status close();

    /// Removes the journal. Called when the capture finished and the archive
    /// stands on its own.
    static Status discard(const std::filesystem::path& archive);

    [[nodiscard]] std::uint64_t bytesWritten() const noexcept { return bytesWritten_; }

private:
    Status append(JournalRecordKind kind, ByteView payload);

    FileStream stream_;
    std::uint64_t bytesWritten_ = 0;
};

/// Reads a journal back, discarding any torn tail.
///
/// Entries are only returned when the block they point into was recorded too.
/// Order in the file is not enough to guarantee that: a capture compresses
/// blocks on several threads, so a block's id is handed out - and the entries
/// going into it resolve - while the block itself is still queued. An entry
/// whose block never reached the journal describes bytes that may not be on
/// the drive at all, and following it would put a location in the manifest
/// pointing at nothing.
///
/// Missing file is reported as `NotFound` - the caller decides whether that
/// means "nothing to resume" or "the journal you named is not there", and the
/// two want different messages.
Result<JournalContents> readTransferJournal(const std::filesystem::path& archive);

/// A stable 64-bit digest of arbitrary bytes, for the fingerprint fields.
///
/// The first eight bytes of a BLAKE2b. The fingerprint guards against
/// resuming into the wrong archive or over a source that has moved on; it is
/// not asked to withstand somebody constructing a collision, because anybody
/// who can write the journal can write the archive beside it.
std::uint64_t journalDigest(ByteView data);

}  // namespace transmit::format
