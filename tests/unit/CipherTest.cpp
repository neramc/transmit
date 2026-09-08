#include <array>
#include <string>

#include <gtest/gtest.h>

#include "format/crypto/ArchiveCipher.h"

namespace transmit::format {
namespace {

/// The encryption, asked directly.
///
/// It had been exercised only through the container - write an archive with a
/// passphrase, read it back - which shows that the happy path works and says
/// nothing about the promises the rest of the design rests on: that a changed
/// bit is refused rather than decrypted into something, that a block cannot be
/// moved to another place in the archive, that two archives written on the
/// same machine with the same passphrase do not share a key.
///
/// Every case here is one of those promises. The suite is skipped whole on a
/// build without OpenSSL, which cannot encrypt at all and says so.
class CipherTest : public testing::Test {
protected:
    void SetUp() override {
        if (!ArchiveCipher::isAvailable()) {
            GTEST_SKIP() << "this build has no OpenSSL, so it cannot encrypt anything";
        }
    }

    /// Cheap parameters. scrypt at the archive's own cost wants 134 MiB and
    /// most of a second, which is right for a passphrase and wrong for forty
    /// cases; one case below uses the real ones.
    [[nodiscard]] static KdfParams cheapParams(Byte fill = Byte{0x11}) {
        KdfParams params;
        params.salt.fill(fill);
        params.logN = 10;
        return params;
    }

    [[nodiscard]] static ArchiveCipher cipherFor(std::string_view passphrase,
                                                 const KdfParams& params) {
        auto cipher = ArchiveCipher::derive(passphrase, params);
        EXPECT_TRUE(cipher) << cipher.error().toString();
        return cipher ? std::move(*cipher) : ArchiveCipher{};
    }

    [[nodiscard]] static ByteBuffer textBytes(std::string_view text) {
        const auto view = asBytes(text);
        return ByteBuffer(view.begin(), view.end());
    }
};

TEST_F(CipherTest, TheSaltIsNotSomethingAnybodyCouldGuess) {
    const auto first = KdfParams::generate();
    const auto second = KdfParams::generate();
    ASSERT_TRUE(first) << first.error().toString();
    ASSERT_TRUE(second) << second.error().toString();

    // All zeroes would give every archive written on this machine the same key
    // for the same passphrase, and nothing downstream would look wrong.
    const ByteBuffer zeroes(KdfParams::kSaltSize);
    EXPECT_FALSE(std::equal(first->salt.begin(), first->salt.end(), zeroes.begin()));
    EXPECT_NE(first->salt, second->salt);
}

TEST_F(CipherTest, TheSamePassphraseAndSaltGiveTheSameKey) {
    const KdfParams params = cheapParams();
    const ArchiveCipher wrote = cipherFor("open sesame", params);
    const ArchiveCipher reads = cipherFor("open sesame", params);

    EXPECT_EQ(wrote.keyCheck(), reads.keyCheck());

    ByteBuffer sealed;
    ASSERT_TRUE(wrote.encrypt(3, ByteView(textBytes("the quick brown fox")), sealed));

    ByteBuffer opened;
    const auto status = reads.decrypt(3, ByteView(sealed), opened);
    ASSERT_TRUE(status) << status.error().toString();
    EXPECT_EQ(opened, textBytes("the quick brown fox"));
}

TEST_F(CipherTest, ADifferentSaltGivesADifferentKey) {
    // This is what stops two archives written on one machine with one
    // passphrase from sharing a key.
    const ArchiveCipher first = cipherFor("open sesame", cheapParams(Byte{0x11}));
    const ArchiveCipher second = cipherFor("open sesame", cheapParams(Byte{0x22}));
    EXPECT_NE(first.keyCheck(), second.keyCheck());

    ByteBuffer sealed;
    ASSERT_TRUE(first.encrypt(0, ByteView(textBytes("secret")), sealed));
    ByteBuffer opened;
    EXPECT_FALSE(second.decrypt(0, ByteView(sealed), opened));
}

TEST_F(CipherTest, ADifferentPassphraseGivesADifferentKey) {
    const KdfParams params = cheapParams();
    const ArchiveCipher right = cipherFor("open sesame", params);
    const ArchiveCipher wrong = cipherFor("open sesamf", params);
    EXPECT_NE(right.keyCheck(), wrong.keyCheck());

    ByteBuffer sealed;
    ASSERT_TRUE(right.encrypt(0, ByteView(textBytes("secret")), sealed));

    ByteBuffer opened;
    const auto refused = wrong.decrypt(0, ByteView(sealed), opened);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::IntegrityMismatch);
}

TEST_F(CipherTest, RefusesAPassphraseThatIsNotOne) {
    EXPECT_FALSE(ArchiveCipher::derive("", cheapParams()));

    // The cost parameter comes out of an archive header, so it is somebody
    // else's number: too small is no protection and too large is a machine
    // that stops responding while it allocates.
    KdfParams tooCheap = cheapParams();
    tooCheap.logN = 9;
    EXPECT_FALSE(ArchiveCipher::derive("open sesame", tooCheap));

    KdfParams tooDear = cheapParams();
    tooDear.logN = 23;
    EXPECT_FALSE(ArchiveCipher::derive("open sesame", tooDear));
}

TEST_F(CipherTest, WhatWentInComesBackWhateverSizeItWas) {
    const ArchiveCipher cipher = cipherFor("open sesame", cheapParams());

    for (const std::size_t length :
         {std::size_t{0}, std::size_t{1}, std::size_t{15}, std::size_t{16}, std::size_t{17},
          std::size_t{4096}, std::size_t{100000}}) {
        ByteBuffer plain(length);
        for (std::size_t i = 0; i < length; ++i) {
            plain[i] = static_cast<Byte>((i * 31 + 7) & 0xFF);
        }

        ByteBuffer sealed;
        ASSERT_TRUE(cipher.encrypt(1, ByteView(plain), sealed)) << length;
        EXPECT_EQ(sealed.size(), length + ArchiveCipher::kTagSize) << length;

        ByteBuffer opened;
        ASSERT_TRUE(cipher.decrypt(1, ByteView(sealed), opened)) << length;
        EXPECT_EQ(opened, plain) << length;
    }
}

TEST_F(CipherTest, EverySingleChangedBitIsRefused) {
    const ArchiveCipher cipher = cipherFor("open sesame", cheapParams());
    const ByteBuffer plain = textBytes("thirty two bytes of plain text..");

    ByteBuffer sealed;
    ASSERT_TRUE(cipher.encrypt(9, ByteView(plain), sealed));

    // Every bit of the body and every bit of the tag. Not a sample: this is
    // the promise the whole design rests on - a damaged archive is refused
    // rather than handed back as something that looks like data.
    for (std::size_t byte = 0; byte < sealed.size(); ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            ByteBuffer damaged = sealed;
            damaged[byte] = static_cast<Byte>(static_cast<unsigned>(damaged[byte]) ^ (1U << bit));

            ByteBuffer opened;
            const auto status = cipher.decrypt(9, ByteView(damaged), opened);
            ASSERT_FALSE(status) << "byte " << byte << " bit " << bit << " went unnoticed";
            EXPECT_EQ(status.error().code, ErrorCode::IntegrityMismatch);
        }
    }
}

TEST_F(CipherTest, ATruncatedBlockIsRefusedAtEveryLength) {
    const ArchiveCipher cipher = cipherFor("open sesame", cheapParams());

    ByteBuffer sealed;
    ASSERT_TRUE(cipher.encrypt(2, ByteView(textBytes("some plain text here")), sealed));

    for (std::size_t length = 0; length < sealed.size(); ++length) {
        ByteBuffer opened;
        const ByteView shortened(sealed.data(), length);
        EXPECT_FALSE(cipher.decrypt(2, shortened, opened)) << "accepted " << length << " bytes";
    }
}

TEST_F(CipherTest, ABlockCannotBeMovedToAnotherPlace) {
    // The block id is both the nonce and the associated data, so a block
    // lifted out of one place in the archive and dropped into another does not
    // decrypt. Without this an archive could be rearranged by somebody who
    // never learned the passphrase.
    const ArchiveCipher cipher = cipherFor("open sesame", cheapParams());

    ByteBuffer sealed;
    ASSERT_TRUE(cipher.encrypt(5, ByteView(textBytes("block five")), sealed));

    for (const std::uint32_t elsewhere : {0u, 4u, 6u, 0xFFFFFFFFu}) {
        ByteBuffer opened;
        const auto status = cipher.decrypt(elsewhere, ByteView(sealed), opened);
        ASSERT_FALSE(status) << "block 5 decrypted as block " << elsewhere;
        EXPECT_EQ(status.error().code, ErrorCode::IntegrityMismatch);
    }

    ByteBuffer opened;
    EXPECT_TRUE(cipher.decrypt(5, ByteView(sealed), opened));
}

TEST_F(CipherTest, ACipherWithNoKeyDoesNothing) {
    const ArchiveCipher empty;
    EXPECT_FALSE(empty.isValid());

    ByteBuffer out;
    EXPECT_FALSE(empty.encrypt(0, ByteView(), out));
    EXPECT_FALSE(empty.decrypt(0, ByteView(), out));
}

TEST_F(CipherTest, MovingACipherLeavesNothingBehindToEncryptWith) {
    ArchiveCipher source = cipherFor("open sesame", cheapParams());
    ASSERT_TRUE(source.isValid());

    const ArchiveCipher moved = std::move(source);
    EXPECT_TRUE(moved.isValid());

    // The key is wiped out of the source, so what is left cannot be used by
    // accident - a cipher that still worked after being moved from would be a
    // second copy of the key nobody meant to keep.
    EXPECT_FALSE(source.isValid());  // NOLINT(bugprone-use-after-move)
    ByteBuffer out;
    EXPECT_FALSE(source.encrypt(0, ByteView(), out));
}

TEST_F(CipherTest, TheRealParametersWork) {
    // One case at the cost an archive actually uses, because the cheap ones
    // above would not notice a change that only breaks the real ones.
    const auto params = KdfParams::generate();
    ASSERT_TRUE(params) << params.error().toString();
    EXPECT_EQ(params->logN, 17u);

    auto cipher = ArchiveCipher::derive("open sesame", *params);
    ASSERT_TRUE(cipher) << cipher.error().toString();

    ByteBuffer sealed;
    ASSERT_TRUE(cipher->encrypt(0, ByteView(textBytes("the real thing")), sealed));
    ByteBuffer opened;
    ASSERT_TRUE(cipher->decrypt(0, ByteView(sealed), opened));
    EXPECT_EQ(opened, textBytes("the real thing"));
}

TEST_F(CipherTest, RandomBytesFillsWhatItIsGiven) {
    ByteBuffer first(64);
    ByteBuffer second(64);
    ASSERT_TRUE(randomBytes(first));
    ASSERT_TRUE(randomBytes(second));

    const ByteBuffer zeroes(64);
    EXPECT_NE(first, zeroes);
    EXPECT_NE(first, second);

    // Asking for nothing is not a failure.
    EXPECT_TRUE(randomBytes(MutableByteView()));
}

}  // namespace
}  // namespace transmit::format
