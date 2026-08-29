#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "format/Bytes.h"
#include "format/Result.h"

namespace transmit::format {

/// Key-derivation parameters stored in the archive header so any Transmit
/// build can re-derive the key from the passphrase.
struct KdfParams {
    static constexpr std::size_t kSaltSize = 16;

    std::array<Byte, kSaltSize> salt{};
    std::uint32_t logN = 17;  ///< scrypt cost; 2^17 needs ~134 MiB
    std::uint32_t blockFactor = 8;
    std::uint32_t parallelism = 1;

    /// Fresh parameters with a random salt.
    ///
    /// Fails rather than returning a salt it could not fill. An all-zero salt
    /// gives every archive written on the machine the same key for the same
    /// passphrase, which turns the encryption into an obfuscation, and the
    /// only place that is visible is here.
    static Result<KdfParams> generate();
};

/// AES-256-GCM over whole blocks.
///
/// Each block gets a distinct nonce derived from its id, and the archive key is
/// derived from a random per-archive salt, so no nonce is ever reused under one
/// key. The authentication tag is appended to the ciphertext, which means a
/// tampered or truncated block fails to decrypt rather than yielding garbage.
class ArchiveCipher {
public:
    static constexpr std::size_t kKeySize = 32;
    static constexpr std::size_t kTagSize = 16;
    static constexpr std::size_t kNonceSize = 12;

    ArchiveCipher();
    ~ArchiveCipher();

    ArchiveCipher(const ArchiveCipher&) = delete;
    ArchiveCipher& operator=(const ArchiveCipher&) = delete;
    ArchiveCipher(ArchiveCipher&& other) noexcept;
    ArchiveCipher& operator=(ArchiveCipher&& other) noexcept;

    /// False when the build has no OpenSSL. Encrypted archives are then
    /// refused with a clear message instead of being silently written in the
    /// clear.
    [[nodiscard]] static bool isAvailable() noexcept;

    static Result<ArchiveCipher> derive(std::string_view passphrase, const KdfParams& params);

    /// Ciphertext is `plain.size() + kTagSize` bytes.
    Status encrypt(std::uint32_t blockId, ByteView plain, ByteBuffer& out) const;
    Status decrypt(std::uint32_t blockId, ByteView cipher, ByteBuffer& out) const;

    /// A value derived from the key but not from the passphrase, stored in the
    /// header so a wrong passphrase is reported immediately instead of after a
    /// failed block decryption.
    [[nodiscard]] std::array<Byte, 16> keyCheck() const;

    [[nodiscard]] bool isValid() const noexcept { return valid_; }

private:
    std::array<Byte, kKeySize> key_{};
    bool valid_ = false;
};

/// Cryptographically secure random bytes. Falls back to std::random_device
/// when OpenSSL is absent; that path is only used for archive ids and salts on
/// builds without encryption.
Status randomBytes(MutableByteView out);

}  // namespace transmit::format
