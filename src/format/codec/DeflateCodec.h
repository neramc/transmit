#pragma once

#include "format/codec/Codec.h"

namespace transmit::format {

/// zlib-backed fallback codec. Always available, so every build can read and
/// write archives even when the optional codecs are missing.
class DeflateCodec final : public ICodec {
public:
    [[nodiscard]] CodecId id() const noexcept override { return CodecId::Deflate; }
    Status compress(ByteView input, const CompressionProfile& profile, ByteBuffer& output,
                    const AbortCheck& abort) const override;
    Status decompress(ByteView input, std::size_t rawSize, ByteBuffer& output) const override;
};

}  // namespace transmit::format
