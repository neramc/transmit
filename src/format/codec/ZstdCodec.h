#pragma once

#include "format/codec/Codec.h"

namespace transmit::format {

/// Default codec. At level 22 with an enlarged window it reaches close to xz
/// ratios on mixed environment data while decompressing far faster, which is
/// what a restore run is bound by.
class ZstdCodec final : public ICodec {
public:
    [[nodiscard]] CodecId id() const noexcept override { return CodecId::Zstd; }
    Status compress(ByteView input, const CompressionProfile& profile, ByteBuffer& output,
                    const AbortCheck& abort) const override;
    Status decompress(ByteView input, std::size_t rawSize, ByteBuffer& output) const override;

    /// Highest level the linked zstd build accepts.
    static int maxLevel() noexcept;
};

}  // namespace transmit::format
