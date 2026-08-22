#include <gtest/gtest.h>

#include <limits>

#include "format/Serialization.h"

namespace transmit::format {
namespace {

TEST(Varint, RoundTripsAcrossTheWholeRange) {
    const std::vector<std::uint64_t> values = {0u,
                                               1u,
                                               127u,
                                               128u,
                                               300u,
                                               16383u,
                                               16384u,
                                               1u << 31,
                                               std::numeric_limits<std::uint64_t>::max()};
    ByteBuffer buffer;
    ByteWriter writer(buffer);
    for (const std::uint64_t value : values) {
        writer.putVarint(value);
    }

    ByteReader reader(buffer);
    for (const std::uint64_t value : values) {
        const auto decoded = reader.getVarint();
        ASSERT_TRUE(decoded) << decoded.error().toString();
        EXPECT_EQ(*decoded, value);
    }
    EXPECT_TRUE(reader.atEnd());
}

TEST(SignedVarint, KeepsNegativeValuesShort) {
    ByteBuffer buffer;
    ByteWriter writer(buffer);
    writer.putSignedVarint(-1);
    EXPECT_EQ(buffer.size(), 1u);

    ByteReader reader(buffer);
    const auto decoded = reader.getSignedVarint();
    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, -1);
}

TEST(SignedVarint, RoundTripsTimestamps) {
    const std::vector<std::int64_t> values = {0, 1, -1, 1710000000000000000LL, -1710000000LL,
                                              std::numeric_limits<std::int64_t>::min(),
                                              std::numeric_limits<std::int64_t>::max()};
    ByteBuffer buffer;
    ByteWriter writer(buffer);
    for (const std::int64_t value : values) {
        writer.putSignedVarint(value);
    }

    ByteReader reader(buffer);
    for (const std::int64_t value : values) {
        const auto decoded = reader.getSignedVarint();
        ASSERT_TRUE(decoded) << decoded.error().toString();
        EXPECT_EQ(*decoded, value);
    }
}

TEST(TaggedFields, RoundTripEveryWireType) {
    ByteBuffer buffer;
    ByteWriter writer(buffer);
    writer.putUInt(1, 42);
    writer.putString(2, "안녕하세요");
    writer.putBool(3, true);
    writer.putInt(4, -9000);
    writer.putDoubleField(5, 1.5);

    ByteReader reader(buffer);
    auto tag = reader.getTag();
    ASSERT_TRUE(tag);
    EXPECT_EQ(tag->field, 1u);
    EXPECT_EQ(*reader.getVarint(), 42u);

    tag = reader.getTag();
    ASSERT_TRUE(tag);
    EXPECT_EQ(tag->field, 2u);
    EXPECT_EQ(*reader.getString(), "안녕하세요");

    tag = reader.getTag();
    ASSERT_TRUE(tag);
    EXPECT_TRUE(*reader.getBool());

    tag = reader.getTag();
    ASSERT_TRUE(tag);
    EXPECT_EQ(*reader.getSignedVarint(), -9000);

    tag = reader.getTag();
    ASSERT_TRUE(tag);
    EXPECT_DOUBLE_EQ(*reader.getDouble(), 1.5);
    EXPECT_TRUE(reader.atEnd());
}

// Forward compatibility is the reason for the tag/length encoding: an older
// build must be able to read an archive that carries fields it never heard of.
TEST(TaggedFields, SkipsUnknownFieldsOfEveryWireType) {
    ByteBuffer buffer;
    ByteWriter writer(buffer);
    writer.putUInt(1, 7);
    writer.putUInt(900, 12345);          // unknown varint
    writer.putString(901, "future");     // unknown bytes
    writer.putDoubleField(902, 2.25);    // unknown fixed64
    writer.putString(2, "still here");

    ByteReader reader(buffer);
    std::uint64_t known = 0;
    std::string trailing;
    while (!reader.atEnd()) {
        const auto tag = reader.getTag();
        ASSERT_TRUE(tag) << tag.error().toString();
        if (tag->field == 1) {
            known = *reader.getVarint();
        } else if (tag->field == 2) {
            trailing = *reader.getString();
        } else {
            ASSERT_TRUE(reader.skip(tag->type));
        }
    }
    EXPECT_EQ(known, 7u);
    EXPECT_EQ(trailing, "still here");
}

TEST(NestedRecords, RoundTrip) {
    ByteBuffer buffer;
    ByteWriter writer(buffer);
    writer.putRecord(1, [](ByteWriter& nested) {
        nested.putString(1, "inner");
        nested.putUInt(2, 99);
    });

    ByteReader reader(buffer);
    const auto tag = reader.getTag();
    ASSERT_TRUE(tag);
    EXPECT_EQ(tag->type, WireType::Bytes);

    const auto payload = reader.getBytes();
    ASSERT_TRUE(payload);

    ByteReader nested(*payload);
    ASSERT_TRUE(nested.getTag());
    EXPECT_EQ(*nested.getString(), "inner");
    ASSERT_TRUE(nested.getTag());
    EXPECT_EQ(*nested.getVarint(), 99u);
    EXPECT_TRUE(nested.atEnd());
}

TEST(ByteReader, RejectsTruncatedInput) {
    ByteBuffer buffer;
    ByteWriter writer(buffer);
    writer.putString(1, "abcdefgh");
    buffer.resize(buffer.size() - 3);  // chop the payload

    ByteReader reader(buffer);
    ASSERT_TRUE(reader.getTag());
    const auto payload = reader.getBytes();
    ASSERT_FALSE(payload);
    EXPECT_EQ(payload.error().code, ErrorCode::EndOfStream);
}

TEST(ByteReader, RejectsAnOverlongVarint) {
    ByteBuffer buffer(12, static_cast<Byte>(0xFF));
    ByteReader reader(buffer);
    const auto decoded = reader.getVarint();
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code, ErrorCode::CorruptArchive);
}

}  // namespace
}  // namespace transmit::format
