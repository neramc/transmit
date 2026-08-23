#include "format/codec/ZstdCodec.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>

#include <zstd.h>

namespace transmit::format {
namespace {

/// Window log used by the Maximum preset. 2^27 = 128 MiB, which lets the solid
/// blocks find matches across an entire block instead of only nearby files.
constexpr int kLongWindowLog = 27;

/// How much is handed to the codec between two abort checks. Small enough to
/// stop promptly, large enough that the per-call overhead does not show.
constexpr std::size_t kSliceBytes = 4ULL * 1024 * 1024;

struct CCtxDeleter {
    void operator()(ZSTD_CCtx* ctx) const noexcept { ZSTD_freeCCtx(ctx); }
};
struct DCtxDeleter {
    void operator()(ZSTD_DCtx* ctx) const noexcept { ZSTD_freeDCtx(ctx); }
};

Error zstdError(std::size_t code, const char* what) {
    return makeError(ErrorCode::CodecFailure, what, ": ", ZSTD_getErrorName(code));
}

}  // namespace

int ZstdCodec::maxLevel() noexcept {
    return ZSTD_maxCLevel();
}

Status ZstdCodec::compress(ByteView input, const CompressionProfile& profile, ByteBuffer& output,
                           const AbortCheck& abort) const {
    const std::unique_ptr<ZSTD_CCtx, CCtxDeleter> ctx(ZSTD_createCCtx());
    if (!ctx) {
        return makeError(ErrorCode::OutOfMemory, "could not create a zstd compression context");
    }

    const int level = std::clamp(profile.level, 1, ZSTD_maxCLevel());
    std::size_t rc = ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_compressionLevel, level);
    if (ZSTD_isError(rc)) {
        return zstdError(rc, "could not set the zstd level");
    }

    if (profile.longWindow) {
        // enableLongDistanceMatching must be set before windowLog, otherwise
        // zstd recomputes the window from the level and drops the request.
        rc = ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_enableLongDistanceMatching, 1);
        if (ZSTD_isError(rc)) {
            return zstdError(rc, "could not enable long distance matching");
        }
        rc = ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_windowLog, kLongWindowLog);
        if (ZSTD_isError(rc)) {
            return zstdError(rc, "could not set the zstd window log");
        }
    }

    // Declared up front so the frame carries the content size and zstd can size
    // its window from it, exactly as the one-shot call would.
    rc = ZSTD_CCtx_setPledgedSrcSize(ctx.get(), input.size());
    if (ZSTD_isError(rc)) {
        return zstdError(rc, "could not declare the zstd input size");
    }

    const std::size_t bound = ZSTD_compressBound(input.size());
    output.resize(bound);

    // Handed over in slices rather than in one call, so that `abort` gets a
    // say. This produces the same frame as compressing in one go; the only
    // difference is that there is somewhere to stop.
    ZSTD_outBuffer out{output.data(), output.size(), 0};
    std::size_t consumed = 0;

    while (true) {
        if (abort && abort()) {
            return Error(ErrorCode::Cancelled);
        }

        const std::size_t slice = std::min(kSliceBytes, input.size() - consumed);
        const bool last = consumed + slice == input.size();
        ZSTD_inBuffer in{input.data() + consumed, slice, 0};
        consumed += slice;

        std::size_t remaining = 0;
        do {
            remaining =
                ZSTD_compressStream2(ctx.get(), &out, &in, last ? ZSTD_e_end : ZSTD_e_continue);
            if (ZSTD_isError(remaining)) {
                return zstdError(remaining, "zstd compression failed");
            }
        } while (in.pos < in.size || (last && remaining != 0));

        if (last) {
            break;
        }
    }

    output.resize(out.pos);
    return ok();
}

Status ZstdCodec::decompress(ByteView input, std::size_t rawSize, ByteBuffer& output) const {
    const std::unique_ptr<ZSTD_DCtx, DCtxDeleter> ctx(ZSTD_createDCtx());
    if (!ctx) {
        return makeError(ErrorCode::OutOfMemory, "could not create a zstd decompression context");
    }

    // A long-window archive needs the matching window limit on the read side.
    const std::size_t rc = ZSTD_DCtx_setParameter(ctx.get(), ZSTD_d_windowLogMax, kLongWindowLog);
    if (ZSTD_isError(rc)) {
        return zstdError(rc, "could not set the zstd window limit");
    }

    output.resize(rawSize);
    const std::size_t produced =
        ZSTD_decompressDCtx(ctx.get(), output.data(), output.size(), input.data(), input.size());
    if (ZSTD_isError(produced)) {
        return zstdError(produced, "zstd decompression failed");
    }
    if (produced != rawSize) {
        return makeError(ErrorCode::CorruptArchive,
                         "decompressed size does not match block header");
    }
    return ok();
}

}  // namespace transmit::format
