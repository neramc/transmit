#include "format/codec/DeflateCodec.h"

#include <zlib.h>

#include <algorithm>
#include <string>

namespace transmit::format {
namespace {

Error zlibError(int code, const char* what) {
    return makeError(ErrorCode::CodecFailure, what, " (zlib code ", std::to_string(code), ")");
}

int clampLevel(int level) { return std::clamp(level, 1, 9); }

}  // namespace

Status DeflateCodec::compress(ByteView input, const CompressionProfile& profile,
                              ByteBuffer& output) const {
    const uLong bound = ::compressBound(static_cast<uLong>(input.size()));
    output.resize(static_cast<std::size_t>(bound));

    uLongf produced = bound;
    const int rc = ::compress2(reinterpret_cast<Bytef*>(output.data()), &produced,
                               reinterpret_cast<const Bytef*>(input.data()),
                               static_cast<uLong>(input.size()), clampLevel(profile.level));
    if (rc != Z_OK) {
        return zlibError(rc, "deflate failed");
    }
    output.resize(static_cast<std::size_t>(produced));
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
