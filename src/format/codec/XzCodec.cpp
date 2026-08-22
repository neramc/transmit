#include "format/codec/XzCodec.h"

#ifdef TRANSMIT_HAVE_LZMA

#include <lzma.h>

#include <algorithm>
#include <string>

namespace transmit::format {
namespace {

Error lzmaError(lzma_ret code, const char* what) {
    return makeError(ErrorCode::CodecFailure, what, " (lzma code ",
                     std::to_string(static_cast<int>(code)), ")");
}

/// The memory limit for decoding. Generous enough for -9e archives while still
/// refusing an archive crafted to exhaust memory on the restoring machine.
constexpr std::uint64_t kDecodeMemoryLimit = 1536ULL * 1024 * 1024;

}  // namespace

Status XzCodec::compress(ByteView input, const CompressionProfile& profile,
                         ByteBuffer& output) const {
    auto preset = static_cast<std::uint32_t>(std::clamp(profile.level, 0, 9));
    // "e" (extreme) spends more time searching for a better ratio.
    preset |= LZMA_PRESET_EXTREME;

    const std::size_t bound = lzma_stream_buffer_bound(input.size());
    output.resize(bound);

    std::size_t produced = 0;
    const lzma_ret rc = lzma_easy_buffer_encode(
        preset, LZMA_CHECK_NONE, nullptr, reinterpret_cast<const std::uint8_t*>(input.data()),
        input.size(), reinterpret_cast<std::uint8_t*>(output.data()), &produced, output.size());
    if (rc != LZMA_OK) {
        return lzmaError(rc, "xz compression failed");
    }
    output.resize(produced);
    return ok();
}

Status XzCodec::decompress(ByteView input, std::size_t rawSize, ByteBuffer& output) const {
    output.resize(rawSize);

    std::uint64_t memoryLimit = kDecodeMemoryLimit;
    std::size_t inPos = 0;
    std::size_t outPos = 0;

    const lzma_ret rc = lzma_stream_buffer_decode(
        &memoryLimit, 0, nullptr, reinterpret_cast<const std::uint8_t*>(input.data()), &inPos,
        input.size(), reinterpret_cast<std::uint8_t*>(output.data()), &outPos, output.size());
    if (rc != LZMA_OK) {
        return lzmaError(rc, "xz decompression failed");
    }
    if (outPos != rawSize) {
        return makeError(ErrorCode::CorruptArchive, "decompressed size does not match block header");
    }
    return ok();
}

}  // namespace transmit::format

#endif  // TRANSMIT_HAVE_LZMA
