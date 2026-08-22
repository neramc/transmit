#include "format/codec/ZstdCodec.h"

#include <algorithm>
#include <memory>
#include <string>

#include <zstd.h>

namespace transmit::format {
namespace {

/// Window log used by the Maximum preset. 2^27 = 128 MiB, which lets the solid
/// blocks find matches across an entire block instead of only nearby files.
constexpr int kLongWindowLog = 27;

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

Status ZstdCodec::compress(ByteView input, const CompressionProfile& profile,
                           ByteBuffer& output) const {
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

    const std::size_t bound = ZSTD_compressBound(input.size());
    output.resize(bound);

    const std::size_t produced =
        ZSTD_compress2(ctx.get(), output.data(), output.size(), input.data(), input.size());
    if (ZSTD_isError(produced)) {
        return zstdError(produced, "zstd compression failed");
    }
    output.resize(produced);
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
