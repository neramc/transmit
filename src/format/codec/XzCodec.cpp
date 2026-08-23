#include "format/codec/XzCodec.h"

#ifdef TRANSMIT_HAVE_LZMA

#include <algorithm>
#include <cstddef>
#include <string>

#include <lzma.h>

namespace transmit::format {
namespace {

Error lzmaError(lzma_ret code, const char* what) {
    return makeError(ErrorCode::CodecFailure, what, " (lzma code ",
                     std::to_string(static_cast<int>(code)), ")");
}

/// The memory limit for decoding. Generous enough for -9e archives while still
/// refusing an archive crafted to exhaust memory on the restoring machine.
constexpr std::uint64_t kDecodeMemoryLimit = 1536ULL * 1024 * 1024;

/// How much is handed to the codec between two abort checks. Small enough to
/// stop promptly, large enough that the per-call overhead does not show.
constexpr std::size_t kSliceBytes = 4ULL * 1024 * 1024;

/// Frees the encoder however the function returns.
struct StreamCloser {
    lzma_stream* stream;
    ~StreamCloser() { lzma_end(stream); }
};

}  // namespace

Status XzCodec::compress(ByteView input, const CompressionProfile& profile, ByteBuffer& output,
                         const AbortCheck& abort) const {
    auto preset = static_cast<std::uint32_t>(std::clamp(profile.level, 0, 9));
    // "e" (extreme) spends more time searching for a better ratio.
    preset |= LZMA_PRESET_EXTREME;

    lzma_stream stream = LZMA_STREAM_INIT;
    const lzma_ret rc = lzma_easy_encoder(&stream, preset, LZMA_CHECK_NONE);
    if (rc != LZMA_OK) {
        return lzmaError(rc, "could not start xz compression");
    }
    const StreamCloser closer{&stream};

    const std::size_t bound = lzma_stream_buffer_bound(input.size());
    output.resize(bound);

    // Handed over in slices rather than in one call, so that `abort` gets a
    // say: at -9e a 64 MiB block is a minute of work that would otherwise have
    // to finish before anything could stop it.
    stream.next_out = reinterpret_cast<std::uint8_t*>(output.data());
    stream.avail_out = output.size();
    std::size_t consumed = 0;

    while (true) {
        if (abort && abort()) {
            return Error(ErrorCode::Cancelled);
        }

        const std::size_t slice = std::min(kSliceBytes, input.size() - consumed);
        const bool last = consumed + slice == input.size();

        stream.next_in = reinterpret_cast<const std::uint8_t*>(input.data() + consumed);
        stream.avail_in = slice;
        consumed += slice;

        lzma_ret status = LZMA_OK;
        while (status == LZMA_OK && (stream.avail_in > 0 || last)) {
            status = lzma_code(&stream, last ? LZMA_FINISH : LZMA_RUN);
        }

        if (last) {
            if (status != LZMA_STREAM_END) {
                return lzmaError(status, "xz compression failed");
            }
            break;
        }
        if (status != LZMA_OK) {
            return lzmaError(status, "xz compression failed");
        }
    }

    output.resize(output.size() - stream.avail_out);
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
        return makeError(ErrorCode::CorruptArchive,
                         "decompressed size does not match block header");
    }
    return ok();
}

}  // namespace transmit::format

#endif  // TRANSMIT_HAVE_LZMA
