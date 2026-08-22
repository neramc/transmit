#pragma once

#include "format/codec/Codec.h"

namespace transmit::format {

/// Pass-through codec. The packer selects it when compression would not pay
/// off (already-compressed media, or a block that grew).
class StoreCodec final : public ICodec {
public:
    [[nodiscard]] CodecId id() const noexcept override { return CodecId::Store; }
    Status compress(ByteView input, const CompressionProfile& profile, ByteBuffer& output) const override;
    Status decompress(ByteView input, std::size_t rawSize, ByteBuffer& output) const override;
};

}  // namespace transmit::format
