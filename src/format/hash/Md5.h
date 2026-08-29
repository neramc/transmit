#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "format/Bytes.h"

namespace transmit::format {

/// MD5 (RFC 1321), for confirming that what came off the drive is what went
/// on to it.
///
/// This is the one thing MD5 is still for and the only thing it is used for
/// here. **Never use it as an identity, a deduplication key, or an
/// authentication tag**: two different files with the same MD5 can be
/// constructed in seconds on a laptop, so anything that treats a matching MD5
/// as "the same file" can be made to accept the wrong one. Content identity,
/// deduplication and block integrity are BLAKE2b's job throughout this format,
/// and encryption authenticates with GCM. What MD5 adds is that a person can
/// check a Transmit archive with `md5sum` on a machine that has never heard of
/// Transmit - which is worth having when the archive is the only copy of
/// somebody's computer.
///
/// Implemented here rather than called through OpenSSL's EVP on purpose: a
/// FIPS-mode build refuses MD5 outright, and verification that quietly becomes
/// optional depending on how the runtime was configured is not verification.
class Md5 {
public:
    static constexpr std::size_t kDigestSize = 16;
    static constexpr std::size_t kBlockSize = 64;

    Md5() = default;

    void update(ByteView data) noexcept;

    /// Writes the digest. Calling it twice returns the same answer rather than
    /// a second, different one.
    void finish(MutableByteView digest) noexcept;

    [[nodiscard]] std::array<Byte, kDigestSize> finish128() noexcept;

    void reset() noexcept;

    static std::array<Byte, kDigestSize> hash(ByteView data);

    /// Lowercase hex, exactly as `md5sum` writes it.
    static std::string hex(ByteView data);

private:
    void compress(const Byte* block) noexcept;

    std::array<std::uint32_t, 4> state_{0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U};
    std::array<Byte, kBlockSize> buffer_{};
    std::uint64_t length_ = 0;  ///< bytes seen, for the length padding
    std::size_t bufferLen_ = 0;
    std::array<Byte, kDigestSize> digest_{};
    bool finished_ = false;
};

using Digest128 = std::array<Byte, Md5::kDigestSize>;

std::string toHex(const Digest128& digest);

}  // namespace transmit::format
