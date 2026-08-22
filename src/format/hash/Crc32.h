#pragma once

#include <cstdint>

#include "format/Bytes.h"

namespace transmit::format {

/// CRC-32 (IEEE 802.3). Used only for the small fixed-size headers, where a
/// cheap corruption check is enough; payload integrity uses BLAKE2b.
std::uint32_t crc32(ByteView data, std::uint32_t seed = 0) noexcept;

}  // namespace transmit::format
