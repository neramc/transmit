#include "format/codec/StoreCodec.h"

namespace transmit::format {

Status StoreCodec::compress(ByteView input, const CompressionProfile&, ByteBuffer& output,
                            const AbortCheck&) const {
    // A copy, with nothing to interrupt.
    output.assign(input.begin(), input.end());
    return ok();
}

Status StoreCodec::decompress(ByteView input, std::size_t rawSize, ByteBuffer& output) const {
    if (input.size() != rawSize) {
        return makeError(ErrorCode::CorruptArchive, "stored block size mismatch");
    }
    output.assign(input.begin(), input.end());
    return ok();
}

}  // namespace transmit::format
