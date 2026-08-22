#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "format/Bytes.h"

namespace transmit::format {

/// BLAKE2b (RFC 7693). Used for content hashing: file identity, deduplication
/// keys, block integrity and manifest verification. Chosen over SHA-256 for
/// throughput on the large scans this application performs, and implemented
/// here so the format layer keeps zero third-party dependencies.
class Blake2b {
public:
    static constexpr std::size_t kMaxDigestSize = 64;
    static constexpr std::size_t kBlockSize = 128;

    explicit Blake2b(std::size_t digestSize = 32);

    void update(ByteView data) noexcept;
    void finish(MutableByteView digest) noexcept;

    [[nodiscard]] std::array<Byte, 32> finish256() noexcept;

    void reset(std::size_t digestSize = 32);

    static std::array<Byte, 32> hash256(ByteView data);
    static std::string hex256(ByteView data);

private:
    void compress(const Byte* block, bool last) noexcept;

    std::array<std::uint64_t, 8> h_{};
    std::array<std::uint64_t, 2> counter_{};
    std::array<Byte, kBlockSize> buffer_{};
    std::size_t bufferLen_ = 0;
    std::size_t digestSize_ = 32;
    bool finished_ = false;
};

/// Convenience alias for the 32-byte digest used throughout the archive format.
using Digest256 = std::array<Byte, 32>;

std::string toHex(const Digest256& digest);

}  // namespace transmit::format
