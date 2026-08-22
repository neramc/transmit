#include "format/hash/Crc32.h"

#include <array>

namespace transmit::format {
namespace {

constexpr std::array<std::uint32_t, 256> makeTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
        }
        table[i] = value;
    }
    return table;
}

constexpr auto kTable = makeTable();

}  // namespace

std::uint32_t crc32(ByteView data, std::uint32_t seed) noexcept {
    std::uint32_t crc = ~seed;
    for (Byte b : data) {
        crc = kTable[(crc ^ static_cast<std::uint8_t>(b)) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

}  // namespace transmit::format
