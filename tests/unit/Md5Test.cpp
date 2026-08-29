// MD5, against RFC 1321's own vectors.
//
// This is the hash a person can check with `md5sum` on a machine that has
// never heard of Transmit, so "close enough" is not a thing it can be: a
// digest that differs from every other implementation is worse than no digest,
// because it reads as corruption on an archive that is perfectly good.

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "format/hash/Md5.h"

namespace transmit::format {
namespace {

std::string hashOf(const std::string& text) {
    return Md5::hex(asBytes(text));
}

// The seven test vectors in RFC 1321 appendix A.5, verbatim.
TEST(Md5, MatchesTheVectorsInRfc1321) {
    EXPECT_EQ(hashOf(""), "d41d8cd98f00b204e9800998ecf8427e");
    EXPECT_EQ(hashOf("a"), "0cc175b9c0f1b6a831c399e269772661");
    EXPECT_EQ(hashOf("abc"), "900150983cd24fb0d6963f7d28e17f72");
    EXPECT_EQ(hashOf("message digest"), "f96b697d7cb7938d525a2f31aaf161d0");
    EXPECT_EQ(hashOf("abcdefghijklmnopqrstuvwxyz"), "c3fcd3d76192e4007dfb496cca67e13b");
    EXPECT_EQ(hashOf("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"),
              "d174ab98d277d9f5a5611c2c9f419d9f");
    EXPECT_EQ(hashOf("123456789012345678901234567890123456789012345678901234567890123456789012"
                     "34567890"),
              "57edf4a22be3c955ac49da2e2107b67a");
}

// The padding rule is the easy part to get wrong: one byte of 0x80, zeros, and
// eight bytes of length - so an input that leaves 56 or more bytes in the last
// block needs a whole extra block, and 55 versus 56 is the boundary.
TEST(Md5, PadsCorrectlyAtEveryBlockBoundary) {
    const std::vector<std::pair<std::size_t, const char*>> vectors = {
        {55, "ef1772b6dff9a122358552954ad0df65"},   {56, "3b0c8ac703f828b04c6c197006d17218"},
        {57, "652b906d60af96844ebd21b674f35e93"},   {63, "b06521f39153d618550606be297466d5"},
        {64, "014842d480b571495a4a0363793f7367"},   {65, "c743a45e0d2e6a95cb859adae0248435"},
        {119, "8a7bd0732ed6a28ce75f6dabc90e1613"},  {120, "5f61c0ccad4cac44c75ff505e1f1e537"},
        {127, "020406e1d05cdc2aa287641f7ae2cc39"},  {128, "e510683b3f5ffe4093d021808bc6ff70"},
        {1000, "cabe45dcc9ae5b66ba86600cca6b8ba8"},
    };
    for (const auto& [length, expected] : vectors) {
        EXPECT_EQ(hashOf(std::string(length, 'a')), expected) << "at length " << length;
    }
}

// Every file over 64 bytes arrives in pieces, and the piece boundaries have
// nothing to do with the block boundaries.
TEST(Md5, StreamingMatchesSinglePass) {
    const std::string text(5000, 'q');

    Md5 streaming;
    std::size_t offset = 0;
    for (const std::size_t chunk : {1U, 7U, 63U, 64U, 65U, 500U, 1U, 1024U}) {
        const std::size_t take = std::min(chunk, text.size() - offset);
        streaming.update(asBytes(std::string_view(text).substr(offset, take)));
        offset += take;
    }
    streaming.update(asBytes(std::string_view(text).substr(offset)));

    EXPECT_EQ(toHex(streaming.finish128()), "2c4922b7659befab6ab40a98e2774a4b");
    EXPECT_EQ(hashOf(text), "2c4922b7659befab6ab40a98e2774a4b");
}

// Every possible split of one input, because the "top up the partial block"
// path is the one that a single chunk size can walk straight past.
TEST(Md5, EverySplitOfTheSameInputAgrees) {
    const std::string text(300, 'z');
    const std::string expected = hashOf(text);

    for (std::size_t split = 0; split <= text.size(); ++split) {
        Md5 md5;
        md5.update(asBytes(std::string_view(text).substr(0, split)));
        md5.update(asBytes(std::string_view(text).substr(split)));
        EXPECT_EQ(toHex(md5.finish128()), expected) << "split at " << split;
    }
}

TEST(Md5, AskingTwiceGivesTheSameAnswer) {
    Md5 md5;
    md5.update(asBytes(std::string_view("abc")));
    const auto first = md5.finish128();
    const auto second = md5.finish128();
    EXPECT_EQ(first, second);

    // And nothing added afterwards can change it, so a digest that has been
    // written into a manifest cannot be quietly rewritten.
    md5.update(asBytes(std::string_view("more")));
    EXPECT_EQ(md5.finish128(), first);
}

TEST(Md5, ResetStartsOver) {
    Md5 md5;
    md5.update(asBytes(std::string_view("something else entirely")));
    (void)md5.finish128();

    md5.reset();
    md5.update(asBytes(std::string_view("abc")));
    EXPECT_EQ(toHex(md5.finish128()), "900150983cd24fb0d6963f7d28e17f72");
}

TEST(Md5, AnEmptyUpdateChangesNothing) {
    Md5 md5;
    md5.update(asBytes(std::string_view("")));
    md5.update(asBytes(std::string_view("ab")));
    md5.update(asBytes(std::string_view("")));
    md5.update(asBytes(std::string_view("c")));
    EXPECT_EQ(toHex(md5.finish128()), "900150983cd24fb0d6963f7d28e17f72");
}

// A digest that differs from `md5sum` by one bit is a false alarm on every
// good archive, so the text form matters as much as the bytes.
TEST(Md5, HexIsLowercaseAndFullWidth) {
    const std::string digest = hashOf("abc");
    EXPECT_EQ(digest.size(), 32U);
    EXPECT_TRUE(std::all_of(digest.begin(), digest.end(), [](const char letter) {
        return (letter >= '0' && letter <= '9') || (letter >= 'a' && letter <= 'f');
    }));
}

}  // namespace
}  // namespace transmit::format
