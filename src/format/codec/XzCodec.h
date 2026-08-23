#pragma once

#include "format/codec/Codec.h"

namespace transmit::format {

/// LZMA2 codec behind the "Extreme" preset. Only present when the build found
/// liblzma; findCodec() returns nullptr otherwise and the preset is rejected
/// with a clear message rather than silently downgraded.
class XzCodec final : public ICodec {
public:
    [[nodiscard]] CodecId id() const noexcept override { return CodecId::Xz; }
    Status compress(ByteView input, const CompressionProfile& profile, ByteBuffer& output,
                    const AbortCheck& abort) const override;
    Status decompress(ByteView input, std::size_t rawSize, ByteBuffer& output) const override;
};

}  // namespace transmit::format
