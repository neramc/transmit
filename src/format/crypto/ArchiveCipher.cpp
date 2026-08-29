#include "format/crypto/ArchiveCipher.h"

#include <algorithm>
#include <random>
#include <utility>

#ifdef TRANSMIT_HAVE_OPENSSL
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#endif

#include "format/hash/Blake2b.h"

namespace transmit::format {
namespace {

#ifdef TRANSMIT_HAVE_OPENSSL
/// scrypt at logN=17 needs ~134 MiB; allow headroom so OpenSSL does not refuse
/// the request on its internal limit.
constexpr std::uint64_t kScryptMaxMemory = 512ULL * 1024 * 1024;

struct CipherContextDeleter {
    void operator()(EVP_CIPHER_CTX* ctx) const noexcept { EVP_CIPHER_CTX_free(ctx); }
};
using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, CipherContextDeleter>;
#endif

std::array<Byte, ArchiveCipher::kNonceSize> nonceFor(std::uint32_t blockId) {
    // Distinct per block; the key is distinct per archive because the salt is
    // random, so this can never repeat under one key.
    std::array<Byte, ArchiveCipher::kNonceSize> nonce{};
    writeLe<std::uint32_t>(MutableByteView(nonce).subspan(0), blockId);
    writeLe<std::uint64_t>(MutableByteView(nonce).subspan(4), 0x5452414e534d4954ULL);
    return nonce;
}

}  // namespace

Status randomBytes(MutableByteView out) {
    if (out.empty()) {
        return ok();
    }
#ifdef TRANSMIT_HAVE_OPENSSL
    if (RAND_bytes(reinterpret_cast<unsigned char*>(out.data()), static_cast<int>(out.size())) ==
        1) {
        return ok();
    }
    return makeError(ErrorCode::Internal, "the system random number generator failed");
#else
    // std::random_device is allowed to be a deterministic sequence, and on at
    // least one mainstream toolchain it is. That is fine for a test fixture and
    // useless for a key, so a build without OpenSSL does not get to pretend it
    // can encrypt - it refuses instead, in the one place that would otherwise
    // hand out a predictable salt.
    (void)out;
    return makeError(ErrorCode::EncryptionUnavailable,
                     "this build of Transmit has no source of cryptographic randomness");
#endif
}

Result<KdfParams> KdfParams::generate() {
    KdfParams params;
    TRANSMIT_CHECK(randomBytes(params.salt));

    // Belt and braces. A generator that reports success and hands back zeroes
    // has been shipped more than once - in virtual machines without an entropy
    // source, and in a few hardware RNG drivers - and the cost of noticing here
    // is one comparison against an outcome whose real probability is 2^-128.
    if (std::all_of(params.salt.begin(), params.salt.end(), [](Byte b) { return b == Byte{0}; })) {
        return makeError(ErrorCode::Internal,
                         "the system random number generator returned nothing but zeroes");
    }
    return params;
}

ArchiveCipher::ArchiveCipher() = default;

ArchiveCipher::~ArchiveCipher() {
    secureZero(key_.data(), key_.size());
}

ArchiveCipher::ArchiveCipher(ArchiveCipher&& other) noexcept
    : key_(other.key_), valid_(std::exchange(other.valid_, false)) {
    secureZero(other.key_.data(), other.key_.size());
}

ArchiveCipher& ArchiveCipher::operator=(ArchiveCipher&& other) noexcept {
    if (this != &other) {
        secureZero(key_.data(), key_.size());
        key_ = other.key_;
        valid_ = std::exchange(other.valid_, false);
        secureZero(other.key_.data(), other.key_.size());
    }
    return *this;
}

bool ArchiveCipher::isAvailable() noexcept {
#ifdef TRANSMIT_HAVE_OPENSSL
    return true;
#else
    return false;
#endif
}

Result<ArchiveCipher> ArchiveCipher::derive(std::string_view passphrase, const KdfParams& params) {
#ifdef TRANSMIT_HAVE_OPENSSL
    if (passphrase.empty()) {
        return makeError(ErrorCode::InvalidArgument, "the passphrase must not be empty");
    }
    if (params.logN < 10 || params.logN > 22) {
        return makeError(ErrorCode::InvalidArgument, "the scrypt cost parameter is out of range");
    }

    ArchiveCipher cipher;
    const std::uint64_t costN = 1ULL << params.logN;

    if (EVP_PBE_scrypt(passphrase.data(), passphrase.size(),
                       reinterpret_cast<const unsigned char*>(params.salt.data()),
                       params.salt.size(), costN, params.blockFactor, params.parallelism,
                       kScryptMaxMemory, reinterpret_cast<unsigned char*>(cipher.key_.data()),
                       cipher.key_.size()) != 1) {
        return makeError(ErrorCode::Internal, "scrypt key derivation failed");
    }
    cipher.valid_ = true;
    return cipher;
#else
    (void)passphrase;
    (void)params;
    return makeError(ErrorCode::EncryptionUnavailable,
                     "this build of Transmit was compiled without OpenSSL, so it cannot read or "
                     "write encrypted archives");
#endif
}

std::array<Byte, 16> ArchiveCipher::keyCheck() const {
    // A hash of the key, not the passphrase, and truncated so it reveals
    // nothing usable while still catching a wrong passphrase up front.
    Blake2b hasher(32);
    hasher.update(asBytes("transmit-key-check-v1"));
    hasher.update(ByteView(key_));
    const auto digest = hasher.finish256();

    std::array<Byte, 16> check{};
    std::copy_n(digest.begin(), check.size(), check.begin());
    return check;
}

Status ArchiveCipher::encrypt(std::uint32_t blockId, ByteView plain, ByteBuffer& out) const {
#ifdef TRANSMIT_HAVE_OPENSSL
    if (!valid_) {
        return makeError(ErrorCode::Internal, "the cipher has no key");
    }

    const CipherContext ctx(EVP_CIPHER_CTX_new());
    if (!ctx) {
        return makeError(ErrorCode::OutOfMemory, "could not create a cipher context");
    }

    const auto nonce = nonceFor(blockId);
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceSize),
                            nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(key_.data()),
                           reinterpret_cast<const unsigned char*>(nonce.data())) != 1) {
        return makeError(ErrorCode::Internal, "could not initialise AES-256-GCM");
    }

    // Bind the block id into the tag so a block cannot be swapped for another.
    std::array<Byte, 4> associated{};
    writeLe<std::uint32_t>(associated, blockId);
    int associatedLength = 0;
    if (EVP_EncryptUpdate(ctx.get(), nullptr, &associatedLength,
                          reinterpret_cast<const unsigned char*>(associated.data()),
                          static_cast<int>(associated.size())) != 1) {
        return makeError(ErrorCode::Internal, "could not bind the block id");
    }

    out.resize(plain.size() + kTagSize);
    int produced = 0;
    if (EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &produced,
                          reinterpret_cast<const unsigned char*>(plain.data()),
                          static_cast<int>(plain.size())) != 1) {
        return makeError(ErrorCode::Internal, "encryption failed");
    }

    int finalProduced = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(out.data()) + produced,
                            &finalProduced) != 1) {
        return makeError(ErrorCode::Internal, "encryption could not be finalised");
    }

    // Widened before the addition, not after: adding two ints and then casting
    // would overflow in int if it overflowed at all, and the cast would then be
    // faithfully preserving a wrong number.
    const auto cipherLength =
        static_cast<std::size_t>(produced) + static_cast<std::size_t>(finalProduced);
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTagSize),
                            out.data() + cipherLength) != 1) {
        return makeError(ErrorCode::Internal, "could not read the authentication tag");
    }
    out.resize(cipherLength + kTagSize);
    return ok();
#else
    (void)blockId;
    (void)plain;
    (void)out;
    return makeError(ErrorCode::EncryptionUnavailable, "this build cannot encrypt");
#endif
}

Status ArchiveCipher::decrypt(std::uint32_t blockId, ByteView cipher, ByteBuffer& out) const {
#ifdef TRANSMIT_HAVE_OPENSSL
    if (!valid_) {
        return makeError(ErrorCode::Internal, "the cipher has no key");
    }
    if (cipher.size() < kTagSize) {
        return makeError(ErrorCode::CorruptArchive, "the encrypted block is truncated");
    }

    const CipherContext ctx(EVP_CIPHER_CTX_new());
    if (!ctx) {
        return makeError(ErrorCode::OutOfMemory, "could not create a cipher context");
    }

    const auto nonce = nonceFor(blockId);
    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceSize),
                            nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(key_.data()),
                           reinterpret_cast<const unsigned char*>(nonce.data())) != 1) {
        return makeError(ErrorCode::Internal, "could not initialise AES-256-GCM");
    }

    std::array<Byte, 4> associated{};
    writeLe<std::uint32_t>(associated, blockId);
    int associatedLength = 0;
    if (EVP_DecryptUpdate(ctx.get(), nullptr, &associatedLength,
                          reinterpret_cast<const unsigned char*>(associated.data()),
                          static_cast<int>(associated.size())) != 1) {
        return makeError(ErrorCode::Internal, "could not bind the block id");
    }

    const std::size_t bodyLength = cipher.size() - kTagSize;
    out.resize(bodyLength);
    int produced = 0;
    if (EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(out.data()), &produced,
                          reinterpret_cast<const unsigned char*>(cipher.data()),
                          static_cast<int>(bodyLength)) != 1) {
        return makeError(ErrorCode::Internal, "decryption failed");
    }

    auto* tag = const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(cipher.data()) +
                                           bodyLength);
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTagSize), tag) !=
        1) {
        return makeError(ErrorCode::Internal, "could not set the authentication tag");
    }

    int finalProduced = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(out.data()) + produced,
                            &finalProduced) != 1) {
        return makeError(ErrorCode::IntegrityMismatch,
                         "the block failed authentication: the archive is damaged or was "
                         "modified after it was written");
    }
    out.resize(static_cast<std::size_t>(produced) + static_cast<std::size_t>(finalProduced));
    return ok();
#else
    (void)blockId;
    (void)cipher;
    (void)out;
    return makeError(ErrorCode::EncryptionUnavailable, "this build cannot decrypt");
#endif
}

}  // namespace transmit::format
