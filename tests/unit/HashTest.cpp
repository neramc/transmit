#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "format/hash/Blake2b.h"
#include "format/hash/Crc32.h"

namespace transmit::format {
namespace {

std::string hashOf(const std::string& text) {
    return Blake2b::hex256(asBytes(text));
}

// Vectors cross-checked against Python's hashlib.blake2b(digest_size=32).
TEST(Blake2b, MatchesKnownVectors) {
    EXPECT_EQ(hashOf(""), "0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8");
    EXPECT_EQ(hashOf("abc"), "bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319");
    EXPECT_EQ(hashOf("The quick brown fox jumps over the lazy dog"),
              "01718cec35cd3d796dd00020e0bfecb473ad23457d063b75eff29c0ffa2e58a9");
}

// The final block must be flagged, so the boundary between "buffer is full" and
// "more input follows" is the easiest place to get BLAKE2b wrong.
TEST(Blake2b, HandlesExactBlockBoundaries) {
    EXPECT_EQ(hashOf(std::string(128, 'a')),
              "ae2aa48507885c4c950fb809b2076f959cde9f8ea6da260d9a3587df33dac450");
    EXPECT_EQ(hashOf(std::string(129, 'a')),
              "2f64744a6de0d2c0b56e64cf6e29a5aaa255010d415d51c75ccc82f73dccd865");
    EXPECT_EQ(hashOf(std::string(1000, 'x')),
              "ab75119ede14ef06ebf31f745fb655ed006cccfe8054c635308a557f7c9beaba");
}

TEST(Blake2b, StreamingMatchesSinglePass) {
    const std::string text(5000, 'q');

    Blake2b streaming(32);
    std::size_t offset = 0;
    for (const std::size_t chunk : {1u, 7u, 127u, 128u, 129u, 500u, 1u}) {
        const std::size_t take = std::min(chunk, text.size() - offset);
        streaming.update(asBytes(std::string_view(text).substr(offset, take)));
        offset += take;
    }
    streaming.update(asBytes(std::string_view(text).substr(offset)));

    EXPECT_EQ(toHex(ByteView(streaming.finish256())), hashOf(text));
}

TEST(Crc32, MatchesKnownVector) {
    // The IEEE CRC-32 of "123456789" is the standard check value.
    EXPECT_EQ(crc32(asBytes("123456789")), 0xCBF43926u);
    EXPECT_EQ(crc32(asBytes("")), 0u);
}

}  // namespace
}  // namespace transmit::format
