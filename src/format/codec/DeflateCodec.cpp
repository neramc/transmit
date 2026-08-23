#include "format/codec/DeflateCodec.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include <zlib.h>

namespace transmit::format {
namespace {

Error zlibError(int code, const char* what) {
    return makeError(ErrorCode::CodecFailure, what, " (zlib code ", std::to_string(code), ")");
}

int clampLevel(int level) {
    return std::clamp(level, 1, 9);
}

/// How much is handed to the codec between two abort checks. Small enough to
/// stop promptly, large enough that the per-call overhead does not show.
constexpr std::size_t kSliceBytes = 4ULL * 1024 * 1024;

/// Ends the stream however the function returns.
struct StreamCloser {
    z_stream* stream;
    ~StreamCloser() { ::deflateEnd(stream); }
};

}  // namespace

Status DeflateCodec::compress(ByteView input, const CompressionProfile& profile, ByteBuffer& output,
                              const AbortCheck& abort) const {
    const uLong bound = ::compressBound(static_cast<uLong>(input.size()));
    output.resize(static_cast<std::size_t>(bound));

    z_stream stream{};
    const int rc = deflateInit(&stream, clampLevel(profile.level));
    if (rc != Z_OK) {
        return zlibError(rc, "could not start deflate");
    }
    const StreamCloser closer{&stream};

    // Fed in slices rather than in one call so that `abort` gets a say. The
    // bytes produced are the same either way: this is one deflate stream, just
    // handed over a bit at a time.
    std::size_t consumed = 0;
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());

    while (true) {
        if (abort && abort()) {
            return Error(ErrorCode::Cancelled);
        }

        const std::size_t slice = std::min(kSliceBytes, input.size() - consumed);
        const bool last = consumed + slice == input.size();

        stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(input.data() + consumed));
        stream.avail_in = static_cast<uInt>(slice);
        consumed += slice;

        int status = Z_OK;
        do {
            status = ::deflate(&stream, last ? Z_FINISH : Z_NO_FLUSH);
        } while (status == Z_OK && (stream.avail_in > 0 || last));

        if (last) {
            if (status != Z_STREAM_END) {
                return zlibError(status, "deflate failed");
            }
            break;
        }
        if (status != Z_OK) {
            return zlibError(status, "deflate failed");
        }
    }

    output.resize(static_cast<std::size_t>(stream.total_out));
    return ok();
}

Status DeflateCodec::decompress(ByteView input, std::size_t rawSize, ByteBuffer& output) const {
    output.resize(rawSize);
    uLongf produced = static_cast<uLongf>(rawSize);
    const int rc = ::uncompress(reinterpret_cast<Bytef*>(output.data()), &produced,
                                reinterpret_cast<const Bytef*>(input.data()),
                                static_cast<uLong>(input.size()));
    if (rc != Z_OK) {
        return zlibError(rc, "inflate failed");
    }
    if (produced != rawSize) {
        return makeError(ErrorCode::CorruptArchive, "inflated size does not match block header");
    }
    return ok();
}

}  // namespace transmit::format
