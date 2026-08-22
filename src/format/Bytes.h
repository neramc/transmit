#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace transmit::format {

using Byte = std::byte;
using ByteBuffer = std::vector<Byte>;
using ByteView = std::span<const Byte>;
using MutableByteView = std::span<Byte>;

inline ByteView asBytes(std::string_view text) noexcept {
    return {reinterpret_cast<const Byte*>(text.data()), text.size()};
}

inline std::string_view asText(ByteView bytes) noexcept {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// Little-endian is the on-disk convention for every fixed-width field so that
/// archives are byte-identical across platforms.
template<typename T>
void writeLe(MutableByteView out, T value) noexcept {
    static_assert(std::is_integral_v<T>, "writeLe expects an integral type");
    using U = std::make_unsigned_t<T>;
    auto raw = static_cast<std::uintmax_t>(static_cast<U>(value));
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out[i] = static_cast<Byte>((raw >> (8 * i)) & 0xFFu);
    }
}

template<typename T>
[[nodiscard]] T readLe(ByteView in) noexcept {
    static_assert(std::is_integral_v<T>, "readLe expects an integral type");
    using U = std::make_unsigned_t<T>;
    std::uintmax_t raw = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        raw |= static_cast<std::uintmax_t>(static_cast<std::uint8_t>(in[i])) << (8 * i);
    }
    return static_cast<T>(static_cast<U>(raw));
}

template<typename T>
void appendLe(ByteBuffer& out, T value) {
    const std::size_t offset = out.size();
    out.resize(offset + sizeof(T));
    writeLe<T>(MutableByteView(out).subspan(offset), value);
}

/// Overwrites `count` bytes with zeroes in a way the optimiser may not remove.
/// Used for passphrase and key material.
void secureZero(void* data, std::size_t count) noexcept;

std::string toHex(ByteView bytes);

}  // namespace transmit::format
