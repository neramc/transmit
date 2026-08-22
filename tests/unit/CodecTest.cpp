#include <random>
#include <string>

#include <gtest/gtest.h>

#include "format/codec/Codec.h"

namespace transmit::format {
namespace {

ByteBuffer compressibleData(std::size_t size) {
    // Repetitive text, the shape most configuration and profile data takes.
    const std::string unit = "user_pref(\"browser.startup.homepage\", \"about:home\");\n";
    ByteBuffer data;
    data.reserve(size);
    while (data.size() < size) {
        const auto bytes = asBytes(unit);
        data.insert(data.end(), bytes.begin(), bytes.end());
    }
    data.resize(size);
    return data;
}

ByteBuffer incompressibleData(std::size_t size) {
    std::mt19937 engine(1234);
    std::uniform_int_distribution<unsigned int> distribution(0, 255);
    ByteBuffer data(size);
    for (Byte& b : data) {
        b = static_cast<Byte>(distribution(engine));
    }
    return data;
}

class CodecRoundTrip : public testing::TestWithParam<CodecId> {};

TEST_P(CodecRoundTrip, RestoresTheExactBytes) {
    const CodecId id = GetParam();
    const ICodec* codec = findCodec(id);
    if (codec == nullptr) {
        GTEST_SKIP() << "the " << codecName(id) << " codec is not in this build";
    }

    const CompressionProfile profile{id, 6, false};
    for (const std::size_t size :
         {std::size_t{0}, std::size_t{1}, std::size_t{1024}, std::size_t{200000}}) {
        const ByteBuffer original = compressibleData(size);

        ByteBuffer compressed;
        ASSERT_TRUE(codec->compress(original, profile, compressed))
            << "size " << size << " with " << codecName(id);

        ByteBuffer restored;
        ASSERT_TRUE(codec->decompress(compressed, original.size(), restored))
            << "size " << size << " with " << codecName(id);
        EXPECT_EQ(restored, original) << "size " << size;
    }
}

TEST_P(CodecRoundTrip, HandlesIncompressibleInput) {
    const CodecId id = GetParam();
    const ICodec* codec = findCodec(id);
    if (codec == nullptr) {
        GTEST_SKIP() << "the " << codecName(id) << " codec is not in this build";
    }

    const ByteBuffer original = incompressibleData(64 * 1024);
    const CompressionProfile profile{id, 6, false};

    ByteBuffer compressed;
    ASSERT_TRUE(codec->compress(original, profile, compressed));

    ByteBuffer restored;
    ASSERT_TRUE(codec->decompress(compressed, original.size(), restored));
    EXPECT_EQ(restored, original);
}

INSTANTIATE_TEST_SUITE_P(AllCodecs, CodecRoundTrip,
                         testing::Values(CodecId::Store, CodecId::Deflate, CodecId::Zstd,
                                         CodecId::Xz),
                         [](const testing::TestParamInfo<CodecId>& info) {
                             return std::string(codecName(info.param));
                         });

TEST(Codec, MaximumPresetActuallyCompresses) {
    const ICodec* codec = findCodec(CodecId::Zstd);
    ASSERT_NE(codec, nullptr);

    const ByteBuffer original = compressibleData(1024 * 1024);
    const auto profile = CompressionProfile::fromPreset(CompressionPreset::Maximum);
    ASSERT_EQ(profile.codec, CodecId::Zstd);
    EXPECT_TRUE(profile.longWindow);

    ByteBuffer compressed;
    ASSERT_TRUE(codec->compress(original, profile, compressed));

    // Highly repetitive input should shrink by orders of magnitude.
    EXPECT_LT(compressed.size(), original.size() / 50);

    ByteBuffer restored;
    ASSERT_TRUE(codec->decompress(compressed, original.size(), restored));
    EXPECT_EQ(restored, original);
}

TEST(Codec, DetectsCorruptedPayload) {
    const ICodec* codec = findCodec(CodecId::Zstd);
    ASSERT_NE(codec, nullptr);

    const ByteBuffer original = compressibleData(50000);
    ByteBuffer compressed;
    ASSERT_TRUE(codec->compress(original, CompressionProfile{CodecId::Zstd, 6, false}, compressed));

    compressed[compressed.size() / 2] =
        static_cast<Byte>(static_cast<std::uint8_t>(compressed[compressed.size() / 2]) ^ 0xFFu);

    ByteBuffer restored;
    const auto status = codec->decompress(compressed, original.size(), restored);
    // Either the codec rejects the frame, or it produces something different -
    // the block hash in the container catches the second case.
    if (status) {
        EXPECT_NE(restored, original);
    } else {
        EXPECT_EQ(status.error().code, ErrorCode::CodecFailure);
    }
}

TEST(Preset, NamesRoundTrip) {
    for (const auto preset : {CompressionPreset::Fast, CompressionPreset::Balanced,
                              CompressionPreset::Maximum, CompressionPreset::Extreme}) {
        const auto parsed = presetFromName(presetName(preset));
        ASSERT_TRUE(parsed) << parsed.error().toString();
        EXPECT_EQ(*parsed, preset);
    }
    EXPECT_FALSE(presetFromName("ludicrous"));
}

TEST(Preset, MapsToTheDocumentedProfiles) {
    EXPECT_EQ(CompressionProfile::fromPreset(CompressionPreset::Fast).level, 6);
    EXPECT_EQ(CompressionProfile::fromPreset(CompressionPreset::Maximum).level, 22);
    EXPECT_EQ(CompressionProfile::fromPreset(CompressionPreset::Extreme).codec, CodecId::Xz);
}

TEST(Codec, StoreAndDeflateAreAlwaysAvailable) {
    EXPECT_TRUE(isCodecAvailable(CodecId::Store));
    EXPECT_TRUE(isCodecAvailable(CodecId::Deflate));
    EXPECT_TRUE(isCodecAvailable(CodecId::Zstd));
}

}  // namespace
}  // namespace transmit::format
