#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "format/Bytes.h"
#include "format/Result.h"

namespace transmit::format {

enum class CodecId : std::uint16_t {
    Store = 0,    ///< no compression; used for already-compressed payloads
    Deflate = 1,  ///< zlib, always available
    Zstd = 2,     ///< default; excellent ratio with fast restore
    Xz = 3,       ///< LZMA2, highest ratio, slowest
};

std::string_view codecName(CodecId id) noexcept;

/// User-facing compression choices. "Maximum" is the default because zstd at
/// its highest level with a 128 MiB window lands within a few percent of xz
/// while decompressing roughly an order of magnitude faster - which matters
/// because restoring an environment is the time-critical half of the job.
enum class CompressionPreset : std::uint8_t {
    Fast,     ///< zstd level 6
    Balanced, ///< zstd level 12
    Maximum,  ///< zstd level 22, long window  (default)
    Extreme,  ///< xz -9e
};

struct CompressionProfile {
    CodecId codec = CodecId::Zstd;
    int level = 22;
    bool longWindow = true;

    static CompressionProfile fromPreset(CompressionPreset preset) noexcept;
};

std::string_view presetName(CompressionPreset preset) noexcept;
Result<CompressionPreset> presetFromName(std::string_view name);

class ICodec {
public:
    virtual ~ICodec() = default;

    [[nodiscard]] virtual CodecId id() const noexcept = 0;

    /// Compresses `input` into `output`, which is resized to the exact size of
    /// the compressed payload. Implementations must be safe to call from
    /// several worker threads at once.
    virtual Status compress(ByteView input, const CompressionProfile& profile,
                            ByteBuffer& output) const = 0;

    /// Decompresses into `output`, which is resized to `rawSize` first. The
    /// caller always knows the uncompressed size from the block header.
    virtual Status decompress(ByteView input, std::size_t rawSize, ByteBuffer& output) const = 0;
};

/// Returns the codec implementation, or nullptr when this build does not
/// include it (for example xz without liblzma).
const ICodec* findCodec(CodecId id) noexcept;

/// True when the codec is compiled into this build.
bool isCodecAvailable(CodecId id) noexcept;

}  // namespace transmit::format
