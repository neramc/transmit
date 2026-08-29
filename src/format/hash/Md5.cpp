#include "format/hash/Md5.h"

#include <cstring>

namespace transmit::format {
namespace {

/// The per-round shift amounts and sine-derived constants of RFC 1321. Written
/// out rather than computed so this file can be read against the RFC directly.
constexpr std::array<std::uint32_t, 64> kShift = {
    7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 5,  9,  14, 20, 5,  9,
    14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
    4,  11, 16, 23, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21};

constexpr std::array<std::uint32_t, 64> kSine = {
    0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU, 0x4787c62aU, 0xa8304613U,
    0xfd469501U, 0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU, 0x6b901122U, 0xfd987193U,
    0xa679438eU, 0x49b40821U, 0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU, 0xd62f105dU,
    0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U, 0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
    0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U, 0x8771f681U, 0x6d9d6122U,
    0xfde5380cU, 0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U, 0x289b7ec6U, 0xeaa127faU,
    0xd4ef3085U, 0x04881d05U, 0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U, 0xf4292244U,
    0x432aff97U, 0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
    0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U, 0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU,
    0xeb86d391U};

constexpr std::uint32_t rotateLeft(std::uint32_t value, std::uint32_t bits) noexcept {
    return (value << bits) | (value >> (32 - bits));
}

std::uint32_t readLittleEndian(const Byte* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void writeLittleEndian(Byte* bytes, std::uint32_t value) noexcept {
    bytes[0] = static_cast<Byte>(value & 0xffU);
    bytes[1] = static_cast<Byte>((value >> 8) & 0xffU);
    bytes[2] = static_cast<Byte>((value >> 16) & 0xffU);
    bytes[3] = static_cast<Byte>((value >> 24) & 0xffU);
}

}  // namespace

void Md5::compress(const Byte* block) noexcept {
    std::array<std::uint32_t, 16> words{};
    for (std::size_t i = 0; i < words.size(); ++i) {
        words[i] = readLittleEndian(block + i * 4);
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];

    for (std::uint32_t i = 0; i < 64; ++i) {
        std::uint32_t mixed = 0;
        std::uint32_t index = 0;
        if (i < 16) {
            mixed = (b & c) | (~b & d);
            index = i;
        } else if (i < 32) {
            mixed = (d & b) | (~d & c);
            index = (5 * i + 1) % 16;
        } else if (i < 48) {
            mixed = b ^ c ^ d;
            index = (3 * i + 5) % 16;
        } else {
            mixed = c ^ (b | ~d);
            index = (7 * i) % 16;
        }

        const std::uint32_t rotated = a + mixed + kSine[i] + words[index];
        a = d;
        d = c;
        c = b;
        b = b + rotateLeft(rotated, kShift[i]);
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
}

void Md5::update(ByteView data) noexcept {
    if (finished_) {
        return;
    }
    const Byte* cursor = data.data();
    std::size_t remaining = data.size();
    length_ += remaining;

    // Top up a partial block first, then take whole blocks straight from the
    // caller's buffer rather than copying every byte through ours.
    if (bufferLen_ > 0) {
        const std::size_t wanted = kBlockSize - bufferLen_;
        const std::size_t taken = remaining < wanted ? remaining : wanted;
        std::memcpy(buffer_.data() + bufferLen_, cursor, taken);
        bufferLen_ += taken;
        cursor += taken;
        remaining -= taken;
        if (bufferLen_ < kBlockSize) {
            return;
        }
        compress(buffer_.data());
        bufferLen_ = 0;
    }

    while (remaining >= kBlockSize) {
        compress(cursor);
        cursor += kBlockSize;
        remaining -= kBlockSize;
    }

    if (remaining > 0) {
        std::memcpy(buffer_.data(), cursor, remaining);
        bufferLen_ = remaining;
    }
}

void Md5::finish(MutableByteView digest) noexcept {
    if (!finished_) {
        const std::uint64_t bits = length_ * 8;

        // A single 0x80 byte, zeros, then the length in bits - which needs
        // eight bytes, so a block with more than 55 bytes in it spills into
        // one more.
        Byte padding[kBlockSize * 2] = {};
        padding[0] = static_cast<Byte>(0x80);
        const std::size_t used = bufferLen_;
        const std::size_t padLength = used < 56 ? 56 - used : 120 - used;

        update(ByteView(padding, padLength));

        Byte lengthBytes[8] = {};
        writeLittleEndian(lengthBytes, static_cast<std::uint32_t>(bits & 0xffffffffU));
        writeLittleEndian(lengthBytes + 4, static_cast<std::uint32_t>(bits >> 32));
        update(ByteView(lengthBytes, sizeof(lengthBytes)));

        for (std::size_t i = 0; i < state_.size(); ++i) {
            writeLittleEndian(digest_.data() + i * 4, state_[i]);
        }
        finished_ = true;
    }

    const std::size_t wanted = digest.size() < kDigestSize ? digest.size() : kDigestSize;
    std::memcpy(digest.data(), digest_.data(), wanted);
}

std::array<Byte, Md5::kDigestSize> Md5::finish128() noexcept {
    std::array<Byte, kDigestSize> digest{};
    finish(MutableByteView(digest.data(), digest.size()));
    return digest;
}

void Md5::reset() noexcept {
    *this = Md5();
}

std::array<Byte, Md5::kDigestSize> Md5::hash(ByteView data) {
    Md5 md5;
    md5.update(data);
    return md5.finish128();
}

std::string Md5::hex(ByteView data) {
    return toHex(hash(data));
}

std::string toHex(const Digest128& digest) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2);
    for (const Byte byte : digest) {
        const auto value = static_cast<unsigned char>(byte);
        text.push_back(kDigits[value >> 4]);
        text.push_back(kDigits[value & 0x0fU]);
    }
    return text;
}

}  // namespace transmit::format
