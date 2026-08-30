#include "format/VolumeSplitter.h"

#include <algorithm>
#include <cstdio>
#include <random>
#include <system_error>

#include "format/hash/Crc32.h"

namespace transmit::format {
namespace {

constexpr std::array<Byte, 8> kVolumeMagic = {Byte{'T'}, Byte{'X'}, Byte{'A'}, Byte{'V'},
                                              Byte{'O'}, Byte{'L'}, Byte{0},   Byte{0}};

/// Where a part header keeps its own checksum, and what that checksum covers.
///
/// Version 3 moved it to the last four bytes so the payload checksum could go
/// in front of it and be covered by it. A checksum field that is not itself
/// protected looks like protection and is not one, so the layout moved rather
/// than the new field being tucked in after the old checksum.
constexpr std::size_t kLegacyCrcOffset = 40;
constexpr std::size_t kPayloadCrcOffset = 40;
constexpr std::size_t kCrcOffset = 44;

/// Three digits of part suffix is the ceiling, which at the FAT32-safe part
/// size is more than three terabytes in one archive.
constexpr std::size_t kMaxPartIndex = 999;

std::string formatPartSuffix(std::uint16_t partIndex) {
    char buffer[8] = {};
    std::snprintf(buffer, sizeof(buffer), ".%03u", static_cast<unsigned>(partIndex));
    return buffer;
}

}  // namespace

ArchiveUuid generateArchiveUuid() {
    std::random_device device;
    std::mt19937_64 engine((static_cast<std::uint64_t>(device()) << 32) ^
                           static_cast<std::uint64_t>(device()));
    std::uniform_int_distribution<std::uint64_t> distribution;

    ArchiveUuid uuid{};
    const std::uint64_t high = distribution(engine);
    const std::uint64_t low = distribution(engine);
    writeLe<std::uint64_t>(MutableByteView(uuid).subspan(0), high);
    writeLe<std::uint64_t>(MutableByteView(uuid).subspan(8), low);

    // RFC 4122 version 4 shape, so the value is recognisable as a random UUID.
    uuid[6] = static_cast<Byte>((static_cast<std::uint8_t>(uuid[6]) & 0x0Fu) | 0x40u);
    uuid[8] = static_cast<Byte>((static_cast<std::uint8_t>(uuid[8]) & 0x3Fu) | 0x80u);
    return uuid;
}

std::string uuidToString(const ArchiveUuid& uuid) {
    const std::string hex = toHex(ByteView(uuid));
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
           hex.substr(16, 4) + "-" + hex.substr(20);
}

Result<ArchiveUuid> uuidFromString(std::string_view text) {
    std::string hex;
    hex.reserve(32);
    for (const char c : text) {
        if (c != '-') {
            hex.push_back(c);
        }
    }
    if (hex.size() != 32) {
        return makeError(ErrorCode::InvalidArgument, "'", std::string(text), "' is not a UUID");
    }

    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };

    ArchiveUuid uuid{};
    for (std::size_t i = 0; i < uuid.size(); ++i) {
        const int high = nibble(hex[i * 2]);
        const int low = nibble(hex[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return makeError(ErrorCode::InvalidArgument, "'", std::string(text),
                             "' contains a non-hexadecimal character");
        }
        uuid[i] = static_cast<Byte>((high << 4) | low);
    }
    return uuid;
}

std::array<Byte, VolumeHeader::kSize> VolumeHeader::encode() const {
    std::array<Byte, kSize> raw{};
    std::copy(kVolumeMagic.begin(), kVolumeMagic.end(), raw.begin());
    writeLe<std::uint16_t>(MutableByteView(raw).subspan(8), version);
    writeLe<std::uint16_t>(MutableByteView(raw).subspan(10), partIndex);
    writeLe<std::uint16_t>(MutableByteView(raw).subspan(12), partCount);
    writeLe<std::uint16_t>(MutableByteView(raw).subspan(14), flags);
    std::copy(archiveUuid.begin(), archiveUuid.end(), raw.begin() + 16);
    writeLe<std::uint64_t>(MutableByteView(raw).subspan(32), payloadLength);

    // The layout follows the version this header declares, not the one this
    // build writes. Anything else and a header saying "version 1" would be
    // written in the version 3 shape, which is a file no reader can make
    // sense of - including this one, which asks the version where to look.
    const std::size_t crcAt = version >= 3 ? kCrcOffset : kLegacyCrcOffset;
    if (version >= 3) {
        writeLe<std::uint32_t>(MutableByteView(raw).subspan(kPayloadCrcOffset), payloadCrc32);
    }
    const std::uint32_t checksum = crc32(ByteView(raw).subspan(0, crcAt));
    writeLe<std::uint32_t>(MutableByteView(raw).subspan(crcAt), checksum);
    return raw;
}

Result<VolumeHeader> VolumeHeader::decode(ByteView data) {
    if (data.size() < kSize) {
        return makeError(ErrorCode::CorruptArchive, "volume header is truncated");
    }
    if (!std::equal(kVolumeMagic.begin(), kVolumeMagic.end(), data.begin())) {
        return makeError(ErrorCode::CorruptArchive, "this file is not a Transmit archive volume");
    }
    // Which bytes the header's own checksum lives in and covers changed at
    // version 3, and the version is inside the part it covers either way, so
    // it is read before the check rather than after it.
    const std::uint16_t declared = readLe<std::uint16_t>(data.subspan(8));
    const std::size_t crcAt = declared >= 3 ? kCrcOffset : kLegacyCrcOffset;
    const std::uint32_t stored = readLe<std::uint32_t>(data.subspan(crcAt));
    const std::uint32_t computed = crc32(data.subspan(0, crcAt));
    if (stored != computed) {
        return makeError(ErrorCode::CorruptArchive, "volume header checksum mismatch");
    }

    VolumeHeader header;
    header.version = declared;
    if (header.version > kVersion) {
        return makeError(ErrorCode::UnsupportedVersion,
                         "this archive volume was written by a newer Transmit");
    }
    header.partIndex = readLe<std::uint16_t>(data.subspan(10));
    header.partCount = readLe<std::uint16_t>(data.subspan(12));
    // Version 1 wrote a zero here and had no notion of finalising, so a set
    // from that era has to be taken at its word.
    header.flags = header.version >= 2 ? readLe<std::uint16_t>(data.subspan(14))
                                       : static_cast<std::uint16_t>(FlagFinalised);
    std::copy_n(data.begin() + 16, header.archiveUuid.size(), header.archiveUuid.begin());
    header.payloadLength = readLe<std::uint64_t>(data.subspan(32));
    if (header.version >= 3) {
        header.payloadCrc32 = readLe<std::uint32_t>(data.subspan(kPayloadCrcOffset));
    }
    return header;
}

std::filesystem::path partPathFor(const std::filesystem::path& basePath, std::uint16_t partIndex) {
    std::filesystem::path path = basePath;
    path += formatPartSuffix(partIndex);
    return path;
}

VolumeSink::~VolumeSink() = default;

Result<std::unique_ptr<VolumeSink>> VolumeSink::create(const std::filesystem::path& basePath,
                                                       std::uint64_t partSize,
                                                       const ArchiveUuid& uuid,
                                                       std::uint64_t syncIntervalBytes) {
    if (partSize > 0 && partSize <= VolumeHeader::kSize) {
        return makeError(ErrorCode::InvalidArgument, "the split size is too small");
    }

    auto sink = std::unique_ptr<VolumeSink>(new VolumeSink());
    sink->basePath_ = basePath;
    sink->partSize_ = partSize;
    sink->syncIntervalBytes_ = syncIntervalBytes;
    sink->uuid_ = uuid;
    TRANSMIT_CHECK(sink->openNextPart());
    return sink;
}

Result<std::unique_ptr<VolumeSink>> VolumeSink::resume(const std::filesystem::path& basePath,
                                                       std::uint64_t partSize,
                                                       const ArchiveUuid& uuid,
                                                       std::uint64_t syncIntervalBytes,
                                                       std::uint64_t logicalLength) {
    if (partSize > 0 && partSize <= VolumeHeader::kSize) {
        return makeError(ErrorCode::InvalidArgument, "the split size is too small");
    }

    // The same naming rule create() uses, walked in order and stopping at the
    // first gap: a set with part 3 missing is not a set with parts 1, 2 and 4.
    std::vector<std::filesystem::path> candidates;
    if (partSize == 0) {
        std::error_code ec;
        if (std::filesystem::exists(basePath, ec)) {
            candidates.push_back(basePath);
        }
    } else {
        for (std::uint16_t index = 1; index <= kMaxPartIndex; ++index) {
            std::filesystem::path candidate = partPathFor(basePath, index);
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec)) {
                break;
            }
            candidates.push_back(std::move(candidate));
        }
    }
    if (candidates.empty()) {
        return makeError(ErrorCode::NotFound, "there is nothing at '", fromFsPath(basePath),
                         "' to carry on from");
    }

    // What each part holds, taken from the file rather than from the header:
    // an interrupted run never patched the header, so its payloadLength is
    // still zero, and the length on disk is the only honest answer.
    std::vector<std::uint64_t> payloads;
    payloads.reserve(candidates.size());
    std::uint64_t total = 0;

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        TRANSMIT_TRY(stream, FileStream::open(candidates[i], FileStream::Mode::Read));

        std::array<Byte, VolumeHeader::kSize> raw{};
        TRANSMIT_CHECK(stream.seek(0));
        TRANSMIT_CHECK(stream.read(raw));
        TRANSMIT_TRY(header, VolumeHeader::decode(raw));

        if (header.archiveUuid != uuid) {
            return makeError(ErrorCode::VolumeOutOfOrder, "'", fromFsPath(candidates[i]),
                             "' was written by a different capture");
        }
        if (header.partIndex != static_cast<std::uint16_t>(i + 1)) {
            return makeError(ErrorCode::VolumeOutOfOrder, "'", fromFsPath(candidates[i]),
                             "' is part ", std::to_string(header.partIndex), " but part ",
                             std::to_string(i + 1), " was expected");
        }
        if (header.isFinalised()) {
            return makeError(ErrorCode::InvalidArgument, "'", fromFsPath(candidates[i]),
                             "' belongs to an archive that was already finished");
        }

        TRANSMIT_TRY(fileSize, stream.size());
        const std::uint64_t payload =
            fileSize > VolumeHeader::kSize ? fileSize - VolumeHeader::kSize : 0;
        payloads.push_back(payload);
        total += payload;
    }

    if (logicalLength > total) {
        return makeError(ErrorCode::InvalidArgument, "asked to carry on at ",
                         std::to_string(logicalLength), " bytes, but the parts beside '",
                         fromFsPath(basePath), "' hold only ", std::to_string(total));
    }

    // Which part the resume point falls in, and how much of it is kept. A
    // point exactly on a boundary stays in the earlier part: the next write
    // opens the following one on its own.
    std::size_t keptParts = 0;
    std::uint64_t consumed = 0;
    std::uint64_t keptInLast = 0;
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        ++keptParts;
        if (consumed + payloads[i] >= logicalLength) {
            keptInLast = logicalLength - consumed;
            break;
        }
        consumed += payloads[i];
    }

    auto sink = std::unique_ptr<VolumeSink>(new VolumeSink());
    sink->basePath_ = basePath;
    sink->partSize_ = partSize;
    sink->syncIntervalBytes_ = syncIntervalBytes;
    sink->uuid_ = uuid;

    // The parts past the resume point go first, so a failure part way through
    // leaves less behind rather than more.
    for (std::size_t i = candidates.size(); i > keptParts; --i) {
        std::error_code ec;
        std::filesystem::remove(candidates[i - 1], ec);
        if (ec) {
            Error error{ErrorCode::IoError, "could not remove '" + fromFsPath(candidates[i - 1]) +
                                                "', which is past the point being resumed from"};
            error.systemCode = ec.value();
            return error;
        }
    }

    const std::filesystem::path& last = candidates[keptParts - 1];
    if (keptInLast != payloads[keptParts - 1]) {
        std::error_code ec;
        std::filesystem::resize_file(last, VolumeHeader::kSize + keptInLast, ec);
        if (ec) {
            Error error{ErrorCode::IoError,
                        "could not cut '" + fromFsPath(last) + "' back to the resume point"};
            error.systemCode = ec.value();
            return error;
        }
    }

    for (std::size_t i = 0; i < keptParts; ++i) {
        sink->parts_.push_back(candidates[i]);
        sink->partPayloads_.push_back(i + 1 == keptParts ? keptInLast : payloads[i]);
    }
    sink->partIndex_ = static_cast<std::uint16_t>(keptParts);
    sink->currentPayload_ = keptInLast;
    // Nobody has checksummed what is already on the drive, and reading it back
    // to find out is the cost this whole path exists to avoid.
    sink->partCrcs_.assign(keptParts, 0);
    sink->payloadChecksumsKnown_ = false;
    sink->logicalOffset_ = logicalLength;

    TRANSMIT_TRY(stream, FileStream::open(last, FileStream::Mode::ReadWrite));
    sink->current_ = std::move(stream);
    TRANSMIT_CHECK(sink->current_.seek(VolumeHeader::kSize + keptInLast));
    return sink;
}

Status VolumeSink::openNextPart() {
    if (partIndex_ >= kMaxPartIndex) {
        return makeError(ErrorCode::InvalidArgument,
                         "the archive would need more than 999 parts; choose a larger split size");
    }
    ++partIndex_;

    // A single-file archive keeps the plain name so it is obvious it is whole.
    const std::filesystem::path path =
        (partSize_ == 0) ? basePath_ : partPathFor(basePath_, partIndex_);

    TRANSMIT_TRY(stream, FileStream::open(path, FileStream::Mode::Write));
    current_ = std::move(stream);

    VolumeHeader header;
    header.partIndex = partIndex_;
    header.partCount = 0;
    header.archiveUuid = uuid_;
    header.payloadLength = 0;
    const auto raw = header.encode();
    TRANSMIT_CHECK(current_.write(ByteView(raw)));

    currentPayload_ = 0;
    currentCrc_ = 0;
    sinceSync_ = 0;
    parts_.push_back(path);
    partPayloads_.push_back(0);
    partCrcs_.push_back(0);
    return ok();
}

Status VolumeSink::closeCurrentPart() {
    if (!current_.isOpen()) {
        return ok();
    }
    // Sync before the handle goes away. fclose only flushes stdio; it makes no
    // promise about the device, so without this a part can be "closed" while
    // its last megabytes are still in the page cache.
    TRANSMIT_CHECK(current_.sync());
    partPayloads_.back() = currentPayload_;
    partCrcs_.back() = currentCrc_;
    current_.close();
    sinceSync_ = 0;
    return ok();
}

Status VolumeSink::write(ByteView data) {
    if (finished_) {
        return makeError(ErrorCode::Internal, "write after the archive was finished");
    }

    while (!data.empty()) {
        std::size_t chunk = data.size();
        if (partSize_ > 0) {
            const std::uint64_t capacity = partSize_ - VolumeHeader::kSize;
            if (currentPayload_ >= capacity) {
                TRANSMIT_CHECK(closeCurrentPart());
                TRANSMIT_CHECK(openNextPart());
            }
            chunk = static_cast<std::size_t>(
                std::min<std::uint64_t>(chunk, capacity - currentPayload_));
        }

        TRANSMIT_CHECK(current_.write(data.subspan(0, chunk)));
        // Accumulated as it goes rather than read back at the end: the bytes
        // are already in hand here, and reading a finished stick back to
        // checksum it would double the slowest part of a capture.
        currentCrc_ = crc32(data.subspan(0, chunk), currentCrc_);
        currentPayload_ += chunk;
        logicalOffset_ += chunk;
        data = data.subspan(chunk);

        if (syncIntervalBytes_ > 0) {
            sinceSync_ += chunk;
            if (sinceSync_ >= syncIntervalBytes_) {
                TRANSMIT_CHECK(current_.sync());
                sinceSync_ = 0;
            }
        }
    }
    return ok();
}

Status VolumeSink::finish() {
    if (finished_) {
        return ok();
    }
    TRANSMIT_CHECK(closeCurrentPart());
    finished_ = true;

    // Patch every part header now that the total is known, so a reader can
    // detect a missing part without scanning the directory.
    //
    // Last part first. A reader starts at part 1 and believes what it says, so
    // part 1 must be the last thing that becomes true: stamped in this order,
    // dying half way through leaves later parts unstamped - which reads as
    // "unfinished", the truth - instead of leaving part 1 vouching for parts
    // that were never completed. Each stamp is synced for the same reason;
    // without that the ordering only exists in the page cache.
    const auto total = static_cast<std::uint16_t>(parts_.size());
    for (std::size_t i = parts_.size(); i-- > 0;) {
        TRANSMIT_TRY(stream, FileStream::open(parts_[i], FileStream::Mode::ReadWrite));
        VolumeHeader header;
        header.partIndex = static_cast<std::uint16_t>(i + 1);
        header.partCount = total;
        header.flags = VolumeHeader::FlagFinalised;
        header.archiveUuid = uuid_;
        header.payloadLength = partPayloads_[i];
        if (payloadChecksumsKnown_) {
            header.flags |= VolumeHeader::FlagPayloadChecksum;
            header.payloadCrc32 = partCrcs_[i];
        }
        const auto raw = header.encode();
        TRANSMIT_CHECK(stream.seek(0));
        TRANSMIT_CHECK(stream.write(ByteView(raw)));
        TRANSMIT_CHECK(stream.sync());
    }

    // And the names themselves, so the set is still there after a power cut.
    const std::filesystem::path parent = basePath_.parent_path();
    TRANSMIT_CHECK(syncDirectory(parent.empty() ? std::filesystem::path(".") : parent));
    return ok();
}

Result<std::unique_ptr<VolumeSource>> VolumeSource::open(const std::filesystem::path& anyPart) {
    // Work out the base name: strip a ".NNN" suffix when one is present.
    std::filesystem::path basePath = anyPart;
    const std::string extension = anyPart.extension().string();
    bool splitNaming = false;
    if (extension.size() == 4 && extension[0] == '.' &&
        std::all_of(extension.begin() + 1, extension.end(),
                    [](char c) { return c >= '0' && c <= '9'; })) {
        basePath = anyPart;
        basePath.replace_extension();
        splitNaming = true;
    }

    auto source = std::unique_ptr<VolumeSource>(new VolumeSource());
    std::vector<std::filesystem::path> candidates;

    if (splitNaming) {
        for (std::uint16_t index = 1; index <= kMaxPartIndex; ++index) {
            std::filesystem::path candidate = partPathFor(basePath, index);
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec)) {
                break;
            }
            candidates.push_back(std::move(candidate));
        }
    } else {
        candidates.push_back(anyPart);
    }

    if (candidates.empty()) {
        return makeError(ErrorCode::NotFound, "no archive volume found at '", fromFsPath(anyPart),
                         "'");
    }

    std::uint16_t declaredCount = 0;
    std::uint64_t logicalStart = 0;

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        TRANSMIT_TRY(stream, FileStream::open(candidates[i], FileStream::Mode::Read));

        // Retried like every other read from the set: the first thing that
        // touches somebody's USB stick should not be the one place a single
        // bad read is fatal.
        std::array<Byte, VolumeHeader::kSize> raw{};
        int attempts = 0;
        const Status headerRead = withRetry(source->retry_, [&stream, &raw, &attempts]() -> Status {
            ++attempts;
            if (auto sought = stream.seek(0); !sought) {
                return sought;
            }
            return stream.read(raw);
        });
        if (attempts > 1) {
            source->retriedReads_ += static_cast<std::uint64_t>(attempts - 1);
        }
        TRANSMIT_CHECK(headerRead);
        TRANSMIT_TRY(header, VolumeHeader::decode(raw));

        if (i == 0) {
            source->uuid_ = header.archiveUuid;
            declaredCount = header.partCount;
            source->finalised_ = header.isFinalised();
        } else if (header.archiveUuid != source->uuid_) {
            return makeError(ErrorCode::VolumeOutOfOrder, "'", fromFsPath(candidates[i]),
                             "' belongs to a different archive");
        }
        if (header.partIndex != static_cast<std::uint16_t>(i + 1)) {
            return makeError(ErrorCode::VolumeOutOfOrder, "'", fromFsPath(candidates[i]),
                             "' is part ", std::to_string(header.partIndex), " but part ",
                             std::to_string(i + 1), " was expected");
        }

        Part part;
        part.logicalStart = logicalStart;
        part.payloadLength = header.payloadLength;
        if (part.payloadLength == 0) {
            // The header was never patched, so the write was interrupted. Fall
            // back to the file size, which is still enough to read what landed.
            TRANSMIT_TRY(fileSize, stream.size());
            part.payloadLength =
                fileSize > VolumeHeader::kSize ? fileSize - VolumeHeader::kSize : 0;
        }
        part.payloadCrc32 = header.payloadCrc32;
        part.hasPayloadCrc = header.hasPayloadChecksum();
        part.stream = std::move(stream);

        logicalStart += part.payloadLength;
        source->parts_.push_back(std::move(part));
        source->partPaths_.push_back(candidates[i]);
    }

    if (declaredCount != 0 && declaredCount != static_cast<std::uint16_t>(candidates.size())) {
        return makeError(ErrorCode::VolumeMissing, "this archive has ",
                         std::to_string(declaredCount), " parts but only ",
                         std::to_string(candidates.size()), " were found next to '",
                         fromFsPath(candidates.front()), "'");
    }

    source->logicalSize_ = logicalStart;
    return source;
}

Status VolumeSource::readAt(std::uint64_t logicalOffset, MutableByteView out) {
    if (logicalOffset + out.size() > logicalSize_) {
        return makeError(ErrorCode::EndOfStream, "read past the end of the archive");
    }

    std::size_t written = 0;
    while (written < out.size()) {
        const std::uint64_t position = logicalOffset + written;

        const auto it = std::find_if(parts_.begin(), parts_.end(), [position](const Part& part) {
            return position >= part.logicalStart &&
                   position < part.logicalStart + part.payloadLength;
        });
        if (it == parts_.end()) {
            return makeError(ErrorCode::VolumeMissing, "no archive part covers offset ",
                             std::to_string(position));
        }

        const std::uint64_t withinPart = position - it->logicalStart;
        const auto available = static_cast<std::size_t>(it->payloadLength - withinPart);
        const std::size_t chunk = std::min(available, out.size() - written);

        // Retried as a unit. Seek-then-read is safe to repeat - the position
        // is set explicitly every time - and this is the one place in Transmit
        // where the bytes are coming off somebody's USB stick, where a read
        // that fails once and works the second time is ordinary rather than
        // remarkable. A full disk or an unplugged stick still fails at once;
        // isTransient decides which is which.
        int attempts = 0;
        const Status attempt = withRetry(retry_, [&]() -> Status {
            ++attempts;
            if (auto sought = it->stream.seek(VolumeHeader::kSize + withinPart); !sought) {
                return sought;
            }
            return it->stream.read(out.subspan(written, chunk));
        });
        if (attempts > 1) {
            retriedReads_ += static_cast<std::uint64_t>(attempts - 1);
        }
        TRANSMIT_CHECK(attempt);
        written += chunk;
    }
    return ok();
}

Status VolumeSource::verifyPayloadChecksums(
    const std::function<bool(std::size_t, std::size_t)>& progress, std::size_t* skipped) {
    if (skipped != nullptr) {
        *skipped = 0;
    }

    // A megabyte at a time. Large enough that the per-read cost disappears,
    // small enough that a part of any size is checked without holding it.
    constexpr std::size_t kChunk = 1024 * 1024;
    ByteBuffer buffer(kChunk);

    for (std::size_t i = 0; i < parts_.size(); ++i) {
        if (progress && !progress(i, parts_.size())) {
            return makeError(ErrorCode::Cancelled, "cancelled");
        }
        Part& part = parts_[i];
        if (!part.hasPayloadCrc) {
            if (skipped != nullptr) {
                ++*skipped;
            }
            continue;
        }

        std::uint32_t running = 0;
        std::uint64_t remaining = part.payloadLength;
        std::uint64_t offset = part.logicalStart;
        while (remaining > 0) {
            const auto take = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, kChunk));
            MutableByteView slice(buffer.data(), take);
            TRANSMIT_CHECK(readAt(offset, slice));
            running = crc32(ByteView(slice), running);
            offset += take;
            remaining -= take;
        }

        if (running != part.payloadCrc32) {
            return makeError(ErrorCode::IntegrityMismatch, "part ", std::to_string(i + 1), " of ",
                             std::to_string(parts_.size()),
                             " does not hold the bytes it was written with");
        }
    }

    if (progress) {
        progress(parts_.size(), parts_.size());
    }
    return ok();
}

}  // namespace transmit::format
