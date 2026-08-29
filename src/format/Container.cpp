#include "format/Container.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <vector>

#include "format/hash/ContentHash.h"
#include "format/hash/Crc32.h"

namespace transmit::format {
namespace {

constexpr std::array<Byte, 4> kArchiveMagic = {Byte{'T'}, Byte{'X'}, Byte{'A'}, Byte{'1'}};
constexpr std::array<Byte, 4> kBlockMagic = {Byte{'T'}, Byte{'X'}, Byte{'A'}, Byte{'B'}};
constexpr std::array<Byte, 4> kFooterMagic = {Byte{'T'}, Byte{'X'}, Byte{'A'}, Byte{'F'}};

constexpr std::size_t kBlockHeaderSize = 48;
constexpr std::size_t kBlockHashPrefix = 12;

/// A compressed block is only kept if it actually saved space; otherwise the
/// raw bytes are stored, which keeps already-compressed media from growing.
constexpr double kMinimumCompressionGain = 0.98;

/// The most a single block may claim to hold when it is read back. Blocks are
/// normally the solid block size, but a file larger than that becomes a block
/// of its own, so the ceiling has to clear any single file somebody might
/// reasonably be carrying - a disk image, a video project - while still being
/// a ceiling. Without one, eight bytes of a corrupt or hostile header ask for
/// an allocation of any size at all, and the process dies before anything has
/// been checked.
constexpr std::uint64_t kMaxBlockRawSize = 64ULL * 1024 * 1024 * 1024;

std::int64_t nowUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct BlockHeaderFields {
    std::uint32_t blockId = 0;
    CodecId codec = CodecId::Zstd;
    std::uint16_t flags = 0;
    std::uint64_t rawSize = 0;
    std::uint64_t storedSize = 0;
    std::array<Byte, kBlockHashPrefix> hashPrefix{};
};

std::array<Byte, kBlockHeaderSize> encodeBlockHeader(const BlockHeaderFields& fields) {
    std::array<Byte, kBlockHeaderSize> raw{};
    std::copy(kBlockMagic.begin(), kBlockMagic.end(), raw.begin());
    writeLe<std::uint32_t>(MutableByteView(raw).subspan(4), fields.blockId);
    writeLe<std::uint16_t>(MutableByteView(raw).subspan(8),
                           static_cast<std::uint16_t>(fields.codec));
    writeLe<std::uint16_t>(MutableByteView(raw).subspan(10), fields.flags);
    writeLe<std::uint64_t>(MutableByteView(raw).subspan(12), fields.rawSize);
    writeLe<std::uint64_t>(MutableByteView(raw).subspan(20), fields.storedSize);
    std::copy(fields.hashPrefix.begin(), fields.hashPrefix.end(), raw.begin() + 28);
    writeLe<std::uint32_t>(MutableByteView(raw).subspan(40), 0);
    const std::uint32_t checksum = crc32(ByteView(raw).subspan(0, 44));
    writeLe<std::uint32_t>(MutableByteView(raw).subspan(44), checksum);
    return raw;
}

Result<BlockHeaderFields> decodeBlockHeader(ByteView raw) {
    if (raw.size() < kBlockHeaderSize) {
        return makeError(ErrorCode::CorruptArchive, "block header is truncated");
    }
    if (!std::equal(kBlockMagic.begin(), kBlockMagic.end(), raw.begin())) {
        return makeError(ErrorCode::CorruptArchive, "block header signature is wrong");
    }
    const std::uint32_t stored = readLe<std::uint32_t>(raw.subspan(44));
    if (stored != crc32(raw.subspan(0, 44))) {
        return makeError(ErrorCode::CorruptArchive, "block header checksum mismatch");
    }

    BlockHeaderFields fields;
    fields.blockId = readLe<std::uint32_t>(raw.subspan(4));
    fields.codec = static_cast<CodecId>(readLe<std::uint16_t>(raw.subspan(8)));
    fields.flags = readLe<std::uint16_t>(raw.subspan(10));
    fields.rawSize = readLe<std::uint64_t>(raw.subspan(12));
    fields.storedSize = readLe<std::uint64_t>(raw.subspan(20));
    std::copy_n(raw.begin() + 28, fields.hashPrefix.size(), fields.hashPrefix.begin());
    return fields;
}

}  // namespace

std::array<Byte, ArchiveHeader::kSize> ArchiveHeader::encode() const {
    std::array<Byte, kSize> raw{};
    std::copy(kArchiveMagic.begin(), kArchiveMagic.end(), raw.begin());
    writeLe<std::uint16_t>(MutableByteView(raw).subspan(4), version);
    writeLe<std::uint16_t>(MutableByteView(raw).subspan(6), flags);
    std::copy(uuid.begin(), uuid.end(), raw.begin() + 8);
    writeLe<std::int64_t>(MutableByteView(raw).subspan(24), createdUnix);
    std::copy(kdf.salt.begin(), kdf.salt.end(), raw.begin() + 32);
    writeLe<std::uint32_t>(MutableByteView(raw).subspan(48), kdf.logN);
    writeLe<std::uint16_t>(MutableByteView(raw).subspan(52),
                           static_cast<std::uint16_t>(kdf.blockFactor));
    writeLe<std::uint16_t>(MutableByteView(raw).subspan(54),
                           static_cast<std::uint16_t>(kdf.parallelism));
    std::copy(keyCheck.begin(), keyCheck.end(), raw.begin() + 56);
    const std::uint32_t checksum = crc32(ByteView(raw).subspan(0, 88));
    writeLe<std::uint32_t>(MutableByteView(raw).subspan(88), checksum);
    return raw;
}

Result<ArchiveHeader> ArchiveHeader::decode(ByteView raw) {
    if (raw.size() < kSize) {
        return makeError(ErrorCode::CorruptArchive, "archive header is truncated");
    }
    if (!std::equal(kArchiveMagic.begin(), kArchiveMagic.end(), raw.begin())) {
        return makeError(ErrorCode::CorruptArchive, "this is not a Transmit archive");
    }
    const std::uint32_t stored = readLe<std::uint32_t>(raw.subspan(88));
    if (stored != crc32(raw.subspan(0, 88))) {
        return makeError(ErrorCode::CorruptArchive, "archive header checksum mismatch");
    }

    ArchiveHeader header;
    header.version = readLe<std::uint16_t>(raw.subspan(4));
    if (header.version > kVersion) {
        return makeError(ErrorCode::UnsupportedVersion,
                         "this archive was written by a newer version of Transmit");
    }
    header.flags = readLe<std::uint16_t>(raw.subspan(6));
    std::copy_n(raw.begin() + 8, header.uuid.size(), header.uuid.begin());
    header.createdUnix = readLe<std::int64_t>(raw.subspan(24));
    std::copy_n(raw.begin() + 32, header.kdf.salt.size(), header.kdf.salt.begin());
    header.kdf.logN = readLe<std::uint32_t>(raw.subspan(48));
    header.kdf.blockFactor = readLe<std::uint16_t>(raw.subspan(52));
    header.kdf.parallelism = readLe<std::uint16_t>(raw.subspan(54));
    std::copy_n(raw.begin() + 56, header.keyCheck.size(), header.keyCheck.begin());
    return header;
}

std::array<Byte, ArchiveFooter::kSize> ArchiveFooter::encode() const {
    std::array<Byte, kSize> raw{};
    std::copy(kFooterMagic.begin(), kFooterMagic.end(), raw.begin());
    writeLe<std::uint64_t>(MutableByteView(raw).subspan(4), manifestOffset);
    writeLe<std::uint64_t>(MutableByteView(raw).subspan(12), manifestRawSize);
    writeLe<std::uint64_t>(MutableByteView(raw).subspan(20), manifestStoredSize);
    writeLe<std::uint64_t>(MutableByteView(raw).subspan(28), entryCount);
    std::copy(manifestHash.begin(), manifestHash.end(), raw.begin() + 36);
    const std::uint32_t checksum = crc32(ByteView(raw).subspan(0, 44));
    writeLe<std::uint32_t>(MutableByteView(raw).subspan(44), checksum);
    return raw;
}

Result<ArchiveFooter> ArchiveFooter::decode(ByteView raw) {
    if (raw.size() < kSize) {
        return makeError(ErrorCode::CorruptArchive, "archive footer is truncated");
    }
    if (!std::equal(kFooterMagic.begin(), kFooterMagic.end(), raw.begin())) {
        return makeError(ErrorCode::CorruptArchive,
                         "the archive has no footer: the write was interrupted");
    }
    const std::uint32_t stored = readLe<std::uint32_t>(raw.subspan(44));
    if (stored != crc32(raw.subspan(0, 44))) {
        return makeError(ErrorCode::CorruptArchive, "archive footer checksum mismatch");
    }

    ArchiveFooter footer;
    footer.manifestOffset = readLe<std::uint64_t>(raw.subspan(4));
    footer.manifestRawSize = readLe<std::uint64_t>(raw.subspan(12));
    footer.manifestStoredSize = readLe<std::uint64_t>(raw.subspan(20));
    footer.entryCount = readLe<std::uint64_t>(raw.subspan(28));
    std::copy_n(raw.begin() + 36, footer.manifestHash.size(), footer.manifestHash.begin());
    return footer;
}

// ----------------------------------------------------------------- writer

ArchiveWriter::~ArchiveWriter() = default;

Result<std::unique_ptr<ArchiveWriter>> ArchiveWriter::create(const std::filesystem::path& basePath,
                                                             const ArchiveOptions& options) {
    auto writer = std::unique_ptr<ArchiveWriter>(new ArchiveWriter());
    writer->options_ = options;
    writer->profile_ = CompressionProfile::fromPreset(options.preset);

    if (!isCodecAvailable(writer->profile_.codec)) {
        return makeError(ErrorCode::UnsupportedCodec, "the '",
                         std::string(presetName(options.preset)), "' preset needs the ",
                         std::string(codecName(writer->profile_.codec)),
                         " codec, which this build does not include");
    }

    writer->header_.uuid = generateArchiveUuid();
    writer->header_.createdUnix = nowUnixSeconds();
    if (options.partSize > 0) {
        writer->header_.flags |= ArchiveHeader::FlagSplit;
    }

    if (!options.passphrase.empty()) {
        if (!ArchiveCipher::isAvailable()) {
            return makeError(ErrorCode::EncryptionUnavailable,
                             "this build of Transmit was compiled without OpenSSL, so it cannot "
                             "write an encrypted archive");
        }
        TRANSMIT_TRY(kdf, KdfParams::generate());
        writer->header_.kdf = kdf;
        TRANSMIT_TRY(cipher, ArchiveCipher::derive(options.passphrase, writer->header_.kdf));
        writer->cipher_ = std::make_unique<ArchiveCipher>(std::move(cipher));
        writer->header_.keyCheck = writer->cipher_->keyCheck();
        writer->header_.flags |= ArchiveHeader::FlagEncrypted;
    }

    TRANSMIT_TRY(sink, VolumeSink::create(basePath, options.partSize, writer->header_.uuid,
                                          options.syncIntervalBytes));
    writer->sink_ = std::move(sink);

    const auto headerBytes = writer->header_.encode();
    TRANSMIT_CHECK(writer->sink_->write(ByteView(headerBytes)));
    return writer;
}

Result<PreparedBlock> ArchiveWriter::prepare(std::uint32_t blockId, ByteView raw,
                                             const AbortCheck& abort) const {
    PreparedBlock block;
    block.blockId = blockId;
    block.rawSize = raw.size();
    block.rawHash = Blake2b::hash256(raw);

    const ICodec* codec = findCodec(profile_.codec);
    if (codec == nullptr) {
        return makeError(ErrorCode::UnsupportedCodec, "the configured codec is not available");
    }

    ByteBuffer compressed;
    TRANSMIT_CHECK(codec->compress(raw, profile_, compressed, abort));

    // Storing beats compressing when the codec barely helped: it costs nothing
    // to decompress later and avoids inflating already-compressed payloads.
    const bool worthCompressing =
        !raw.empty() && static_cast<double>(compressed.size()) <
                            static_cast<double>(raw.size()) * kMinimumCompressionGain;

    if (worthCompressing) {
        block.codec = profile_.codec;
        block.payload = std::move(compressed);
    } else {
        block.codec = CodecId::Store;
        block.payload.assign(raw.begin(), raw.end());
    }

    if (cipher_ != nullptr) {
        ByteBuffer sealed;
        TRANSMIT_CHECK(cipher_->encrypt(blockId, block.payload, sealed));
        block.payload = std::move(sealed);
        block.encrypted = true;
    }
    return block;
}

Status ArchiveWriter::writeRecord(const PreparedBlock& block, bool isManifest) {
    BlockHeaderFields fields;
    fields.blockId = block.blockId;
    fields.codec = block.codec;
    fields.flags = block.encrypted ? 1u : 0u;
    fields.rawSize = block.rawSize;
    fields.storedSize = block.payload.size();
    std::copy_n(block.rawHash.begin(), fields.hashPrefix.size(), fields.hashPrefix.begin());

    const std::uint64_t offset = sink_->logicalOffset();
    const auto headerBytes = encodeBlockHeader(fields);
    TRANSMIT_CHECK(sink_->write(ByteView(headerBytes)));
    TRANSMIT_CHECK(sink_->write(block.payload));

    storedBytes_ += kBlockHeaderSize + block.payload.size();

    if (!isManifest) {
        BlockRecord record;
        record.blockId = block.blockId;
        record.streamOffset = offset;
        record.rawSize = block.rawSize;
        record.storedSize = block.payload.size();
        record.codec = block.codec;
        record.encrypted = block.encrypted;
        blocks_.push_back(record);
    }
    return ok();
}

Status ArchiveWriter::writePrepared(const PreparedBlock& block) {
    if (finished_) {
        return makeError(ErrorCode::Internal, "the archive is already finished");
    }
    return writeRecord(block, false);
}

Result<std::uint32_t> ArchiveWriter::writeBlock(ByteView raw) {
    const std::uint32_t blockId = nextBlockId();
    TRANSMIT_TRY(block, prepare(blockId, raw));
    TRANSMIT_CHECK(writePrepared(block));
    return blockId;
}

Status ArchiveWriter::finish(Manifest& manifest) {
    if (finished_) {
        return ok();
    }

    manifest.archiveId = uuidToString(header_.uuid);
    manifest.preset = options_.preset;
    manifest.encrypted = header_.isEncrypted();
    manifest.blocks = blocks_;

    manifest.totalRawBytes = 0;
    for (const auto& record : blocks_) {
        manifest.totalRawBytes += record.rawSize;
    }

    const ByteBuffer serialized = manifest.serialize();
    TRANSMIT_TRY(manifestBlock, prepare(kManifestBlockId, serialized));

    ArchiveFooter footer;
    footer.manifestOffset = sink_->logicalOffset();
    footer.manifestRawSize = serialized.size();
    footer.manifestStoredSize = manifestBlock.payload.size();
    footer.entryCount = manifest.entries.size();
    std::copy_n(manifestBlock.rawHash.begin(), footer.manifestHash.size(),
                footer.manifestHash.begin());

    TRANSMIT_CHECK(writeRecord(manifestBlock, true));

    const auto footerBytes = footer.encode();
    TRANSMIT_CHECK(sink_->write(ByteView(footerBytes)));
    storedBytes_ += ArchiveFooter::kSize;

    TRANSMIT_CHECK(sink_->finish());
    finished_ = true;

    manifest.totalStoredBytes = storedBytes_;
    return ok();
}

const std::vector<std::filesystem::path>& ArchiveWriter::parts() const {
    return sink_->parts();
}

// ----------------------------------------------------------------- reader

ArchiveReader::~ArchiveReader() = default;

Result<std::unique_ptr<ArchiveReader>> ArchiveReader::open(const std::filesystem::path& anyPart) {
    auto reader = std::unique_ptr<ArchiveReader>(new ArchiveReader());

    TRANSMIT_TRY(source, VolumeSource::open(anyPart));
    reader->source_ = std::move(source);

    // The part headers are stamped last, after every byte is on the device, so
    // an unstamped set means the write never got there. Reading one would hand
    // back whatever happened to land, which for a restore is the worst
    // possible answer: plausible, incomplete, and silent about it.
    if (!reader->source_->finalised()) {
        return makeError(ErrorCode::CorruptArchive, "'", fromFsPath(anyPart),
                         "' was never finished - the capture that wrote it was interrupted");
    }

    if (reader->source_->logicalSize() < ArchiveHeader::kSize + ArchiveFooter::kSize) {
        return makeError(ErrorCode::CorruptArchive, "the archive is too small to be valid");
    }

    std::array<Byte, ArchiveHeader::kSize> headerBytes{};
    TRANSMIT_CHECK(reader->source_->readAt(0, headerBytes));
    TRANSMIT_TRY(header, ArchiveHeader::decode(headerBytes));
    reader->header_ = header;

    std::array<Byte, ArchiveFooter::kSize> footerBytes{};
    TRANSMIT_CHECK(reader->source_->readAt(reader->source_->logicalSize() - ArchiveFooter::kSize,
                                           footerBytes));
    TRANSMIT_TRY(footer, ArchiveFooter::decode(footerBytes));
    reader->footer_ = footer;

    if (reader->header_.uuid != reader->source_->uuid()) {
        return makeError(ErrorCode::CorruptArchive,
                         "the archive header and its volumes disagree about the archive id");
    }
    return reader;
}

Status ArchiveReader::unlock(std::string_view passphrase) {
    if (!isEncrypted()) {
        return ok();
    }
    if (!ArchiveCipher::isAvailable()) {
        return makeError(ErrorCode::EncryptionUnavailable,
                         "this archive is encrypted, but this build of Transmit was compiled "
                         "without OpenSSL");
    }

    TRANSMIT_TRY(cipher, ArchiveCipher::derive(passphrase, header_.kdf));
    if (cipher.keyCheck() != header_.keyCheck) {
        return makeError(ErrorCode::WrongPassphrase, "that passphrase does not open this archive");
    }
    cipher_ = std::make_unique<ArchiveCipher>(std::move(cipher));
    return ok();
}

Result<const Manifest*> ArchiveReader::manifest() {
    if (manifest_.has_value()) {
        return &manifest_.value();
    }
    if (isEncrypted() && cipher_ == nullptr) {
        return makeError(ErrorCode::WrongPassphrase,
                         "this archive is encrypted: unlock it before reading its contents");
    }

    BlockRecord record;
    record.blockId = kManifestBlockId;
    record.streamOffset = footer_.manifestOffset;
    record.rawSize = footer_.manifestRawSize;
    record.storedSize = footer_.manifestStoredSize;
    record.encrypted = isEncrypted();

    TRANSMIT_TRY(raw, loadBlock(record));

    // The footer is the last thing written and the only thing that says an
    // archive is finished, so it gets to have an opinion about which manifest
    // belongs to it. The block's own header proves it is intact; this proves
    // it is the one the footer committed to. Read since the format was
    // written, compared for the first time here.
    const Digest256 digest = Blake2b::hash256(raw);
    if (!std::equal(footer_.manifestHash.begin(), footer_.manifestHash.end(), digest.begin())) {
        return makeError(ErrorCode::IntegrityMismatch,
                         "the manifest is not the one this archive was closed with");
    }

    TRANSMIT_TRY(parsed, Manifest::deserialize(raw));
    manifest_ = std::move(parsed);

    if (Status located = validateEntryLocations(); !located) {
        manifest_.reset();
        return located.error();
    }

    // A repair archive beside this one is picked up without being asked for,
    // so every reader - verify, inspect, restore - sees the same archive. An
    // encrypted one needs its passphrase, which this class deliberately does
    // not keep, so those are attached by the caller that has it.
    if (!repairChecked_ && !isEncrypted()) {
        repairChecked_ = true;
        const std::filesystem::path candidate = repairPathFor(source_->parts().front());
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            // Ignored on failure: a repair file that is unreadable, or belongs
            // to a different capture, is a reason to fall back to the original
            // rather than a reason to refuse to open it.
            (void)attachRepair(candidate);
        }
    }
    return &manifest_.value();
}

Status ArchiveReader::validateEntryLocations() const {
    if (!manifest_.has_value()) {
        return ok();
    }

    // Blocks by id, so a hundred thousand entries do not each walk the block
    // list looking for theirs.
    std::unordered_map<std::uint32_t, const BlockRecord*> blocks;
    blocks.reserve(manifest_->blocks.size());
    for (const BlockRecord& block : manifest_->blocks) {
        blocks.emplace(block.blockId, &block);
    }

    for (const ManifestEntry& entry : manifest_->entries) {
        if (!entry.hasContent()) {
            continue;
        }
        const auto found = blocks.find(entry.location.blockId);
        if (found == blocks.end()) {
            return makeError(ErrorCode::CorruptArchive, "'", entry.path.toDisplayString(),
                             "' names a block that is not in this archive");
        }
        // Checked here rather than when the entry is read, so an archive whose
        // table is wrong says so when it is opened - not half way through a
        // restore that has already written a thousand files.
        const std::uint64_t end = entry.location.offset + entry.location.length;
        if (end < entry.location.offset || end > found->second->rawSize) {
            return makeError(ErrorCode::CorruptArchive, "'", entry.path.toDisplayString(),
                             "' points outside block ", std::to_string(entry.location.blockId));
        }
        if (entry.location.length != entry.size) {
            return makeError(ErrorCode::CorruptArchive, "'", entry.path.toDisplayString(),
                             "' is a different length from the space reserved for it");
        }
    }
    return ok();
}

Status ArchiveReader::verifyEntryIn(const ManifestEntry& entry, ByteView block) const {
    const std::uint64_t end = entry.location.offset + entry.location.length;
    if (end > block.size()) {
        return makeError(ErrorCode::CorruptArchive, "'", entry.path.toDisplayString(),
                         "' points outside its block");
    }

    const ByteView slice = block.subspan(static_cast<std::size_t>(entry.location.offset),
                                         static_cast<std::size_t>(entry.location.length));
    const ContentDigests digests = hashContent(slice, entry.hasMd5());

    if (digests.blake2b != entry.contentHash) {
        return makeError(ErrorCode::IntegrityMismatch, "'", entry.path.toDisplayString(),
                         "' does not match its recorded hash");
    }
    if (entry.hasMd5() && digests.md5 != entry.contentMd5) {
        return makeError(ErrorCode::IntegrityMismatch, "'", entry.path.toDisplayString(),
                         "' does not match its recorded MD5");
    }
    return ok();
}

Result<ByteBuffer> ArchiveReader::loadBlock(const BlockRecord& record) {
    std::array<Byte, kBlockHeaderSize> headerBytes{};
    TRANSMIT_CHECK(source_->readAt(record.streamOffset, headerBytes));
    TRANSMIT_TRY(fields, decodeBlockHeader(headerBytes));

    if (fields.blockId != record.blockId) {
        return makeError(ErrorCode::CorruptArchive, "block ", std::to_string(record.blockId),
                         " is not where the directory says it is");
    }

    // Everything below happens before a single byte is allocated on the word of
    // the header. The header's own CRC only proves it is the header that was
    // written; it says nothing about whether the sizes in it are sane, and
    // both of them are used as allocation sizes a few lines further down.
    //
    // The directory - the manifest for a data block, the footer for the
    // manifest itself - independently recorded the same two numbers when the
    // archive was written, so disagreement means one of the two is damaged and
    // neither can be trusted.
    if (fields.storedSize != record.storedSize || fields.rawSize != record.rawSize) {
        return makeError(ErrorCode::CorruptArchive, "block ", std::to_string(record.blockId),
                         " disagrees with the directory about its size");
    }
    // A block cannot hold more stored bytes than the archive actually contains.
    // This one is exact rather than a guess: the parts are on disk and their
    // combined length is known.
    if (record.streamOffset + kBlockHeaderSize > source_->logicalSize() ||
        fields.storedSize > source_->logicalSize() - record.streamOffset - kBlockHeaderSize) {
        return makeError(ErrorCode::CorruptArchive, "block ", std::to_string(record.blockId),
                         " claims more data than the archive holds");
    }
    if (fields.rawSize > kMaxBlockRawSize) {
        return makeError(ErrorCode::CorruptArchive, "block ", std::to_string(record.blockId),
                         " claims to unpack to ", std::to_string(fields.rawSize),
                         " bytes, which is beyond anything Transmit writes");
    }

    ByteBuffer stored(static_cast<std::size_t>(fields.storedSize));
    TRANSMIT_CHECK(source_->readAt(record.streamOffset + kBlockHeaderSize, stored));

    ByteBuffer payload;
    if ((fields.flags & 1u) != 0) {
        if (cipher_ == nullptr) {
            return makeError(ErrorCode::WrongPassphrase, "this block is encrypted and locked");
        }
        TRANSMIT_CHECK(cipher_->decrypt(fields.blockId, stored, payload));
    } else {
        payload = std::move(stored);
    }

    const ICodec* codec = findCodec(fields.codec);
    if (codec == nullptr) {
        return makeError(ErrorCode::UnsupportedCodec, "this archive uses the ",
                         std::string(codecName(fields.codec)),
                         " codec, which this build does not include");
    }

    ByteBuffer raw;
    TRANSMIT_CHECK(codec->decompress(payload, static_cast<std::size_t>(fields.rawSize), raw));

    const auto digest = Blake2b::hash256(raw);
    if (!std::equal(fields.hashPrefix.begin(), fields.hashPrefix.end(), digest.begin())) {
        return makeError(ErrorCode::IntegrityMismatch, "block ", std::to_string(record.blockId),
                         " does not match its recorded hash");
    }
    return raw;
}

Result<ByteView> ArchiveReader::readBlock(std::uint32_t blockId) {
    if (const auto it = blockCache_.find(blockId); it != blockCache_.end()) {
        return ByteView(it->second);
    }

    TRANSMIT_TRY(loaded, manifest());
    const BlockRecord* record = loaded->findBlock(blockId);
    if (record == nullptr) {
        return makeError(ErrorCode::NotFound, "the archive has no block ", std::to_string(blockId));
    }

    TRANSMIT_TRY(raw, loadBlock(*record));

    // A solid block usually serves many consecutive entries, so a small cache
    // removes most repeated decompression during a restore.
    while (cacheOrder_.size() >= cacheLimit_ && !cacheOrder_.empty()) {
        blockCache_.erase(cacheOrder_.front());
        cacheOrder_.erase(cacheOrder_.begin());
    }
    cacheOrder_.push_back(blockId);
    auto [it, inserted] = blockCache_.emplace(blockId, std::move(raw));
    (void)inserted;
    return ByteView(it->second);
}

std::filesystem::path ArchiveReader::repairPathFor(const std::filesystem::path& archive) {
    std::filesystem::path repair = archive;
    repair += ".repair";
    return repair;
}

Status ArchiveReader::attachRepair(const std::filesystem::path& repairPath,
                                   std::string_view passphrase) {
    TRANSMIT_TRY(loaded, manifest());

    auto opening = ArchiveReader::open(repairPath);
    if (!opening) {
        return opening.error();
    }
    auto candidate = std::move(opening).value();

    if (candidate->isEncrypted()) {
        if (passphrase.empty()) {
            return makeError(ErrorCode::WrongPassphrase,
                             "the repair archive is encrypted and no passphrase was given");
        }
        TRANSMIT_CHECK(candidate->unlock(passphrase));
    }
    TRANSMIT_TRY(repaired, candidate->manifest());

    // What the original says about each of its own files. A repair is only
    // allowed to supply bytes for a path the original already has, hashing to
    // what the original already recorded - so a repair file that has been
    // tampered with, or belongs to a different capture, supplies nothing.
    std::map<std::string, const ManifestEntry*> wanted;
    for (const ManifestEntry& entry : loaded->entries) {
        if (entry.hasContent()) {
            wanted.emplace(entry.path.toDisplayString(), &entry);
        }
    }

    std::map<std::string, const ManifestEntry*> accepted;
    for (const ManifestEntry& entry : repaired->entries) {
        if (!entry.hasContent()) {
            continue;
        }
        const auto original = wanted.find(entry.path.toDisplayString());
        if (original == wanted.end()) {
            continue;
        }
        if (entry.contentHash != original->second->contentHash ||
            entry.size != original->second->size) {
            continue;
        }
        accepted.emplace(entry.path.toDisplayString(), &entry);
    }

    if (accepted.empty()) {
        return makeError(ErrorCode::NotFound, "'", fromFsPath(repairPath),
                         "' holds nothing this archive is missing");
    }

    repair_ = std::move(candidate);
    repairs_ = std::move(accepted);
    return ok();
}

const ManifestEntry* ArchiveReader::repairFor(const ManifestEntry& entry) const {
    if (repair_ == nullptr) {
        return nullptr;
    }
    const auto found = repairs_.find(entry.path.toDisplayString());
    return found == repairs_.end() ? nullptr : found->second;
}

Result<ByteBuffer> ArchiveReader::readEntry(const ManifestEntry& entry) {
    if (!entry.hasContent()) {
        return ByteBuffer{};
    }

    // The original first, always. A repair archive exists because part of the
    // drive went bad, and the parts that did not are still the best copy -
    // reading from the repair by default would quietly hide a drive getting
    // worse.
    if (const ManifestEntry* replacement = repairFor(entry); replacement != nullptr) {
        auto original = readEntryFromThisArchive(entry);
        if (original) {
            return original;
        }
        return repair_->readEntry(*replacement);
    }

    return readEntryFromThisArchive(entry);
}

Result<ByteBuffer> ArchiveReader::readEntryFromThisArchive(const ManifestEntry& entry) {
    TRANSMIT_TRY(block, readBlock(entry.location.blockId));
    if (entry.location.offset + entry.location.length > block.size()) {
        return makeError(ErrorCode::CorruptArchive, "'", entry.path.toDisplayString(),
                         "' points outside its block");
    }

    const ByteView slice = block.subspan(static_cast<std::size_t>(entry.location.offset),
                                         static_cast<std::size_t>(entry.location.length));

    const auto digest = Blake2b::hash256(slice);
    if (digest != entry.contentHash) {
        return makeError(ErrorCode::IntegrityMismatch, "'", entry.path.toDisplayString(),
                         "' does not match its recorded hash");
    }
    return ByteBuffer(slice.begin(), slice.end());
}

Status ArchiveReader::verifyAllBlocks(
    const std::function<bool(std::size_t done, std::size_t total)>& progress) {
    TRANSMIT_TRY(loaded, manifest());
    const std::size_t total = loaded->blocks.size();

    // The entries that live in each block, so a block is decompressed once and
    // everything inside it is checked while it is in hand. Going the other way
    // round - a pass over the entries - would decompress a 64 MiB block again
    // for every one of the thousands of files in it.
    std::unordered_map<std::uint32_t, std::vector<const ManifestEntry*>> byBlock;
    for (const ManifestEntry& entry : loaded->entries) {
        if (entry.hasContent()) {
            byBlock[entry.location.blockId].push_back(&entry);
        }
    }

    for (std::size_t i = 0; i < total; ++i) {
        const BlockRecord& record = loaded->blocks[i];

        // loadBlock already checks the hash and, when encrypted, the GCM tag.
        TRANSMIT_TRY(raw, loadBlock(record));

        if (const auto found = byBlock.find(record.blockId); found != byBlock.end()) {
            for (const ManifestEntry* entry : found->second) {
                TRANSMIT_CHECK(verifyEntryIn(*entry, raw));
            }
        }

        if (progress && !progress(i + 1, total)) {
            return makeError(ErrorCode::Cancelled, "verification was cancelled");
        }
    }
    return ok();
}

void ArchiveReader::setBlockCacheLimit(std::size_t blocks) noexcept {
    cacheLimit_ = blocks == 0 ? 1 : blocks;
}

std::size_t ArchiveReader::partCount() const {
    return source_->partCount();
}

const std::vector<std::filesystem::path>& ArchiveReader::parts() const {
    return source_->parts();
}

std::uint64_t ArchiveReader::retriedReads() const {
    return source_->retriedReads();
}

}  // namespace transmit::format
