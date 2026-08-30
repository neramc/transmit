#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "format/Bytes.h"
#include "format/Manifest.h"
#include "format/Result.h"
#include "format/VolumeSplitter.h"
#include "format/codec/Codec.h"
#include "format/crypto/ArchiveCipher.h"
#include "format/hash/Blake2b.h"

namespace transmit::format {

/// Reserved block id for the manifest, so it can travel through the same
/// compression and encryption path as the data blocks.
inline constexpr std::uint32_t kManifestBlockId = 0xFFFFFFFFu;

/// Default amount of uncompressed data packed into one solid block. Larger
/// blocks compress better because the codec can match across files, but every
/// worker holds one block in memory while compressing, and a partial restore
/// has to decompress a whole block to reach one file. 64 MiB balances the two.
inline constexpr std::uint64_t kDefaultSolidBlockSize = 64ULL * 1024 * 1024;

struct ArchiveHeader {
    static constexpr std::size_t kSize = 96;

    /// What this build writes.
    ///
    /// Version 2 differs from 1 in three places, all of them things version 1
    /// had no room for: the footer carries the manifest's whole hash rather
    /// than its first eight bytes, each block header carries sixteen bytes of
    /// its hash rather than twelve, and each part records a checksum of the
    /// payload it holds rather than only of its own header.
    static constexpr std::uint16_t kVersion = 2;

    /// The oldest this build can read. Archives written by version 1 are still
    /// opened and restored - there is no reason to strand one - which is what
    /// the committed fixture in the tests exists to keep true.
    static constexpr std::uint16_t kOldestReadableVersion = 1;

    enum Flags : std::uint16_t {
        FlagEncrypted = 1u << 0,
        FlagSplit = 1u << 1,
    };

    std::uint16_t version = kVersion;
    std::uint16_t flags = 0;
    ArchiveUuid uuid{};
    std::int64_t createdUnix = 0;
    KdfParams kdf;
    std::array<Byte, 16> keyCheck{};

    [[nodiscard]] bool isEncrypted() const noexcept { return (flags & FlagEncrypted) != 0; }

    [[nodiscard]] std::array<Byte, kSize> encode() const;
    static Result<ArchiveHeader> decode(ByteView data);
};

/// The last thing written, and the only thing that says an archive is finished.
///
/// Version 1 recorded the first eight bytes of the manifest's hash, which was
/// as much as would fit. Eight bytes is enough to catch a damaged manifest and
/// not enough to be called a commitment: it is the one place in the format
/// where the archive says "this is the manifest I closed with", and saying it
/// in sixty-four bits was a compromise with the layout rather than a decision.
/// Version 2 carries all thirty-two.
///
/// Both are read. A version 1 footer is forty-eight bytes ending in "TXAF"'s
/// magic at its start; a version 2 footer is eighty ending in "TXAG"'s. The
/// reader tries the longer one first and falls back, so an archive written
/// before this change opens exactly as it did.
struct ArchiveFooter {
    static constexpr std::size_t kSize = 80;
    static constexpr std::size_t kLegacySize = 48;

    std::uint64_t manifestOffset = 0;
    std::uint64_t manifestRawSize = 0;
    std::uint64_t manifestStoredSize = 0;
    std::uint64_t entryCount = 0;
    Digest256 manifestHash{};

    /// How many bytes of `manifestHash` the footer actually committed to.
    /// Eight for a version 1 archive, thirty-two for a version 2. Comparing
    /// more than were written would fail every version 1 archive.
    std::size_t hashBytes = sizeof(Digest256);

    [[nodiscard]] std::array<Byte, kSize> encode() const;

    /// Reads a version 2 footer. `data` must be the last kSize bytes.
    static Result<ArchiveFooter> decode(ByteView data);

    /// Reads a version 1 footer. `data` must be the last kLegacySize bytes.
    static Result<ArchiveFooter> decodeLegacy(ByteView data);
};

struct ArchiveOptions {
    CompressionPreset preset = CompressionPreset::Maximum;

    /// 0 writes a single file. Use kFat32SafePartSize for FAT32 targets.
    std::uint64_t partSize = 0;

    /// Empty means no encryption. A non-empty passphrase is required whenever
    /// the capture includes the Secrets domain.
    std::string passphrase;

    std::uint64_t solidBlockSize = kDefaultSolidBlockSize;

    /// How often the payload is pushed to the device while writing. Zero syncs
    /// only when a part closes and at finish(). Removable media should set it
    /// - 32 MiB is the usual choice - so a stick that is full or has been
    /// pulled says so part way through rather than at the very end.
    std::uint64_t syncIntervalBytes = 0;

    /// Record each file's MD5 alongside its BLAKE2b hash.
    ///
    /// Eighteen bytes an entry - about 18 kB for a hundred thousand files -
    /// for the ability to check the archive with `md5sum` on a machine that
    /// has never heard of Transmit. On by default for that reason.
    bool recordMd5 = true;
};

/// A block that has been compressed (and encrypted) but not yet written. The
/// expensive part is pure and can run on any worker thread; only writePrepared
/// touches the output stream.
struct PreparedBlock {
    std::uint32_t blockId = 0;
    CodecId codec = CodecId::Zstd;
    bool encrypted = false;
    std::uint64_t rawSize = 0;
    Digest256 rawHash{};
    ByteBuffer payload;
};

class ArchiveWriter {
public:
    ~ArchiveWriter();

    ArchiveWriter(const ArchiveWriter&) = delete;
    ArchiveWriter& operator=(const ArchiveWriter&) = delete;

    static Result<std::unique_ptr<ArchiveWriter>> create(const std::filesystem::path& basePath,
                                                         const ArchiveOptions& options);

    /// Where an interrupted capture got to, in the terms the writer needs.
    ///
    /// This comes from the transfer journal beside the archive, never from the
    /// archive itself: an unfinished archive has no manifest and no footer, so
    /// the only way to read its blocks back would be to walk a tail that was
    /// being written when the run stopped.
    struct ResumePoint {
        /// Length of the logical stream to carry on from. Everything past it
        /// is cut off.
        std::uint64_t logicalLength = 0;

        /// The blocks already on the drive, in the order they were written.
        /// They go into the manifest unchanged when the capture finishes.
        std::vector<BlockRecord> blocks;
    };

    /// Carries on writing an archive a previous run left unfinished.
    ///
    /// The header is read back off the drive rather than made afresh, and that
    /// is the whole reason resuming is possible at all: it carries the
    /// archive's uuid, and - for an encrypted archive - the salt the existing
    /// blocks were encrypted under. Generating a new one would leave every
    /// block already written unreadable while looking perfectly well formed.
    ///
    /// The passphrase is checked against the header before anything is
    /// written, so resuming with the wrong one fails in milliseconds instead
    /// of producing an archive whose two halves need different keys.
    static Result<std::unique_ptr<ArchiveWriter>> resume(const std::filesystem::path& basePath,
                                                         const ArchiveOptions& options,
                                                         const ResumePoint& point);

    /// Reserves the next block id. Safe to call before the block's bytes are
    /// ready, so the pipeline can keep ids in scan order.
    [[nodiscard]] std::uint32_t nextBlockId() noexcept { return nextBlockId_++; }

    /// Compresses and encrypts without touching the output; callable from any
    /// thread. `abort`, when given, can stop a long compression part way.
    [[nodiscard]] Result<PreparedBlock> prepare(std::uint32_t blockId, ByteView raw,
                                                const AbortCheck& abort = {}) const;

    /// Appends a prepared block. Must be called from the writer thread only.
    Status writePrepared(const PreparedBlock& block);

    /// Convenience for tests and small payloads: prepare then write.
    Result<std::uint32_t> writeBlock(ByteView raw);

    /// Writes the manifest and footer and closes every volume. The manifest is
    /// updated in place with the block directory and the storage totals.
    Status finish(Manifest& manifest);

    /// The blocks written so far, in the order they were written.
    [[nodiscard]] const std::vector<BlockRecord>& blocks() const noexcept { return blocks_; }

    /// How long the logical stream is. This is the offset a later run would
    /// carry on from, and the only one a journal should ever record.
    [[nodiscard]] std::uint64_t logicalLength() const noexcept;

    [[nodiscard]] const ArchiveUuid& uuid() const noexcept { return header_.uuid; }
    [[nodiscard]] bool isEncrypted() const noexcept { return header_.isEncrypted(); }
    [[nodiscard]] const std::vector<std::filesystem::path>& parts() const;
    [[nodiscard]] std::uint64_t storedBytes() const noexcept { return storedBytes_; }
    [[nodiscard]] const ArchiveOptions& options() const noexcept { return options_; }

private:
    ArchiveWriter() = default;

    Status writeRecord(const PreparedBlock& block, bool isManifest);

    ArchiveOptions options_;
    CompressionProfile profile_;
    ArchiveHeader header_;
    std::unique_ptr<ArchiveCipher> cipher_;
    std::unique_ptr<VolumeSink> sink_;
    std::vector<BlockRecord> blocks_;
    std::uint32_t nextBlockId_ = 1;
    std::uint64_t storedBytes_ = 0;
    bool finished_ = false;
};

class ArchiveReader {
public:
    ~ArchiveReader();

    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;

    /// Opens the header. For an encrypted archive the manifest stays sealed
    /// until unlock() succeeds, so even the file names are protected.
    static Result<std::unique_ptr<ArchiveReader>> open(const std::filesystem::path& anyPart);

    [[nodiscard]] bool isEncrypted() const noexcept { return header_.isEncrypted(); }
    [[nodiscard]] bool isUnlocked() const noexcept { return !isEncrypted() || cipher_ != nullptr; }

    /// Verifies the passphrase against the header's key check before doing any
    /// real work, so a typo is reported in milliseconds.
    Status unlock(std::string_view passphrase);

    Result<const Manifest*> manifest();

    /// Decompresses a whole block. Recently used blocks are cached because a
    /// solid block usually holds many consecutive entries.
    Result<ByteView> readBlock(std::uint32_t blockId);

    /// Extracts one entry's bytes and verifies them against the stored hash.
    ///
    /// When a repair archive is attached and holds a copy of this entry, the
    /// copy is used - see attachRepair.
    Result<ByteBuffer> readEntry(const ManifestEntry& entry);

    /// Attaches a repair archive, so files the drive damaged can be read from
    /// a second, smaller archive written beside the first.
    ///
    /// A damaged part cannot be edited in place: the footer and every part
    /// length are computed over the whole set, so patching one file inside it
    /// would invalidate the archive it was meant to fix. Writing the recovered
    /// files into `name.txa.repair` and reading them from there leaves the
    /// original exactly as it is, which is also what makes the operation safe
    /// to interrupt.
    ///
    /// A repair may only supply bytes that hash to what this archive's own
    /// manifest already recorded for that path. It cannot introduce content
    /// the original never claimed - so a repair file that has been tampered
    /// with, or belongs to a different capture, changes nothing.
    Status attachRepair(const std::filesystem::path& repairPath, std::string_view passphrase = {});

    /// Whether a repair archive is in use.
    [[nodiscard]] bool hasRepair() const noexcept { return repair_ != nullptr; }

    /// The name a repair archive takes for this one.
    [[nodiscard]] static std::filesystem::path repairPathFor(const std::filesystem::path& archive);

    [[nodiscard]] const ArchiveUuid& uuid() const noexcept { return header_.uuid; }
    [[nodiscard]] std::int64_t createdUnix() const noexcept { return header_.createdUnix; }

    /// Which version of the format wrote this archive. It decides how much of
    /// each hash was committed to, so anything checking those has to ask.
    [[nodiscard]] std::uint16_t version() const noexcept { return header_.version; }

    [[nodiscard]] std::size_t partCount() const;
    [[nodiscard]] const std::vector<std::filesystem::path>& parts() const;

    /// Reads that needed more than one attempt since this reader was opened.
    [[nodiscard]] std::uint64_t retriedReads() const;

    /// Reads every block and checks everything inside it.
    ///
    /// Each block is decompressed once - which checks the block's own hash and,
    /// when encrypted, its GCM tag - and then every file that lives in it is
    /// checked against its recorded size, its BLAKE2b hash, and its MD5 where
    /// one was recorded. Checking the blocks alone, which is all this used to
    /// do, proves the archive decompresses; it does not prove that the entry
    /// table still points at the right bytes.
    Status verifyAllBlocks(
        const std::function<bool(std::size_t done, std::size_t total)>& progress);

    /// How many decompressed blocks to keep. One is the minimum.
    ///
    /// Each one costs the solid block size in memory - 64 MiB by default - so
    /// this is a memory-against-decompression trade and the caller is the only
    /// one who knows which side it is on.
    void setBlockCacheLimit(std::size_t blocks) noexcept;

    /// How many blocks were served from the cache rather than decompressed
    /// again. Exposed so a test can prove the cache is one, rather than
    /// assuming it from the fact that it exists.
    [[nodiscard]] std::uint64_t cacheHits() const noexcept { return cacheHits_; }
    [[nodiscard]] std::uint64_t blocksDecompressed() const noexcept { return blocksRead_; }

private:
    ArchiveReader() = default;

    Result<ByteBuffer> loadBlock(const BlockRecord& record);

    /// Checks one entry against the bytes of the block it lives in.
    Status verifyEntryIn(const ManifestEntry& entry, ByteView block) const;

    /// Checks that every entry points somewhere that exists, before anything
    /// tries to read one. Run once, when the manifest is first loaded.
    [[nodiscard]] Status validateEntryLocations() const;

    /// The repair archive's entry for this path, or nullptr.
    [[nodiscard]] const ManifestEntry* repairFor(const ManifestEntry& entry) const;

    /// Reads an entry without considering any attached repair.
    Result<ByteBuffer> readEntryFromThisArchive(const ManifestEntry& entry);

    /// Moves a cached block to the most-recently-used end.
    void touch(std::uint32_t blockId);

    ArchiveHeader header_;
    ArchiveFooter footer_;
    std::unique_ptr<VolumeSource> source_;
    std::unique_ptr<ArchiveCipher> cipher_;
    std::optional<Manifest> manifest_;

    /// The repair archive, and its entries by the path they stand in for.
    /// Held by value rather than merged into manifest_ so the original's own
    /// record of itself is never rewritten by something read off the drive
    /// beside it.
    std::unique_ptr<ArchiveReader> repair_;
    std::map<std::string, const ManifestEntry*> repairs_;
    bool repairChecked_ = false;

    std::map<std::uint32_t, ByteBuffer> blockCache_;
    std::vector<std::uint32_t> cacheOrder_;
    std::size_t cacheLimit_ = 3;
    std::uint64_t cacheHits_ = 0;
    std::uint64_t blocksRead_ = 0;
};

}  // namespace transmit::format
