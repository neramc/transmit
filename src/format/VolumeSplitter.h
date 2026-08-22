#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "format/Bytes.h"
#include "format/FileIo.h"
#include "format/Result.h"

namespace transmit::format {

/// Identifier shared by every part of one archive, so a part from a different
/// run cannot be mixed into the set.
using ArchiveUuid = std::array<Byte, 16>;

ArchiveUuid generateArchiveUuid();
std::string uuidToString(const ArchiveUuid& uuid);
Result<ArchiveUuid> uuidFromString(std::string_view text);

/// FAT32 cannot hold a file of 4 GiB or more, and USB sticks are still shipped
/// formatted that way. 3.5 GiB leaves room for the part header and for a
/// filesystem that reports its limit slightly optimistically.
inline constexpr std::uint64_t kFat32SafePartSize = 3584ULL * 1024 * 1024;

/// Every part file starts with this header so a set can be validated and
/// ordered even if the files were renamed or copied in the wrong sequence.
struct VolumeHeader {
    static constexpr std::size_t kSize = 48;
    static constexpr std::uint16_t kVersion = 1;

    std::uint16_t version = kVersion;
    std::uint16_t partIndex = 1;  ///< 1-based
    std::uint16_t partCount = 0;  ///< 0 until the write finishes
    ArchiveUuid archiveUuid{};
    std::uint64_t payloadLength = 0;

    [[nodiscard]] std::array<Byte, kSize> encode() const;
    static Result<VolumeHeader> decode(ByteView data);
};

/// Writes one logical byte stream across as many part files as needed.
class VolumeSink {
public:
    VolumeSink() = default;
    ~VolumeSink();

    VolumeSink(const VolumeSink&) = delete;
    VolumeSink& operator=(const VolumeSink&) = delete;

    /// `basePath` is the archive path without a part suffix, for example
    /// "/media/usb/home.txa". With `partSize == 0` a single file of that exact
    /// name is written; otherwise parts are named "home.txa.001", ".002", ...
    static Result<std::unique_ptr<VolumeSink>> create(const std::filesystem::path& basePath,
                                                      std::uint64_t partSize,
                                                      const ArchiveUuid& uuid);

    Status write(ByteView data);

    /// Logical offset of the next byte, ignoring part headers. Block offsets
    /// recorded in the manifest are in this space.
    [[nodiscard]] std::uint64_t logicalOffset() const noexcept { return logicalOffset_; }

    /// Closes the last part and patches every header with the final part count
    /// and payload length.
    Status finish();

    [[nodiscard]] const std::vector<std::filesystem::path>& parts() const noexcept {
        return parts_;
    }

private:
    Status openNextPart();
    Status closeCurrentPart();

    std::filesystem::path basePath_;
    std::uint64_t partSize_ = 0;
    ArchiveUuid uuid_{};
    FileStream current_;
    std::uint64_t currentPayload_ = 0;
    std::uint64_t logicalOffset_ = 0;
    std::uint16_t partIndex_ = 0;
    std::vector<std::filesystem::path> parts_;
    std::vector<std::uint64_t> partPayloads_;
    bool finished_ = false;
};

/// Reads a logical byte stream back from a set of part files.
class VolumeSource {
public:
    /// Accepts either the single-file archive or any one of its parts; the
    /// rest of the set is discovered from the naming pattern.
    static Result<std::unique_ptr<VolumeSource>> open(const std::filesystem::path& anyPart);

    /// Reads `out.size()` bytes starting at a logical offset, transparently
    /// crossing part boundaries.
    Status readAt(std::uint64_t logicalOffset, MutableByteView out);

    [[nodiscard]] std::uint64_t logicalSize() const noexcept { return logicalSize_; }
    [[nodiscard]] const ArchiveUuid& uuid() const noexcept { return uuid_; }
    [[nodiscard]] std::size_t partCount() const noexcept { return parts_.size(); }
    [[nodiscard]] const std::vector<std::filesystem::path>& parts() const noexcept {
        return partPaths_;
    }

private:
    struct Part {
        FileStream stream;
        std::uint64_t logicalStart = 0;
        std::uint64_t payloadLength = 0;
    };

    std::vector<Part> parts_;
    std::vector<std::filesystem::path> partPaths_;
    ArchiveUuid uuid_{};
    std::uint64_t logicalSize_ = 0;
};

/// Builds the "name.txa.007" style path for a part index.
std::filesystem::path partPathFor(const std::filesystem::path& basePath, std::uint16_t partIndex);

}  // namespace transmit::format
