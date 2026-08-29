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
    static constexpr std::uint16_t kVersion = 1;

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

struct ArchiveFooter {
    static constexpr std::size_t kSize = 48;

    std::uint64_t manifestOffset = 0;
    std::uint64_t manifestRawSize = 0;
    std::uint64_t manifestStoredSize = 0;
    std::uint64_t entryCount = 0;
    std::array<Byte, 8> manifestHash{};

    [[nodiscard]] std::array<Byte, kSize> encode() const;
    static Result<ArchiveFooter> decode(ByteView data);
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
