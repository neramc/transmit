#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "format/BlockPacker.h"

namespace transmit::format {
namespace {

/// Collects the blocks a packer emits so tests can inspect grouping directly.
struct RecordingSink {
    std::vector<ByteBuffer> blocks;

    BlockPacker::BlockSink asSink() {
        return [this](ByteView raw) -> Result<std::uint32_t> {
            blocks.emplace_back(raw.begin(), raw.end());
            return static_cast<std::uint32_t>(blocks.size());  // ids start at 1
        };
    }
};

ByteBuffer bytesOf(std::string_view text) {
    const auto view = asBytes(text);
    return ByteBuffer(view.begin(), view.end());
}

TEST(BlockPacker, GroupsSmallFilesIntoOneSolidBlock) {
    RecordingSink sink;
    BlockPacker packer(1024, sink.asSink());

    const auto first = packer.add(bytesOf("alpha"));
    const auto second = packer.add(bytesOf("bravo"));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_TRUE(sink.blocks.empty()) << "nothing should be written before the block fills";

    ASSERT_TRUE(packer.flush());
    ASSERT_EQ(sink.blocks.size(), 1u);
    EXPECT_EQ(asText(sink.blocks[0]), "alphabravo");

    const auto firstLocation = packer.location(*first);
    const auto secondLocation = packer.location(*second);
    ASSERT_TRUE(firstLocation);
    ASSERT_TRUE(secondLocation);
    EXPECT_EQ(firstLocation->blockId, secondLocation->blockId);
    EXPECT_EQ(firstLocation->offset, 0u);
    EXPECT_EQ(firstLocation->length, 5u);
    EXPECT_EQ(secondLocation->offset, 5u);
}

TEST(BlockPacker, StartsANewBlockWhenTheCurrentOneIsFull) {
    RecordingSink sink;
    BlockPacker packer(10, sink.asSink());

    ASSERT_TRUE(packer.add(bytesOf("aaaaa")));
    ASSERT_TRUE(packer.add(bytesOf("bbbbb")));
    ASSERT_TRUE(packer.add(bytesOf("ccccc")));  // does not fit, flushes the first
    ASSERT_TRUE(packer.flush());

    ASSERT_EQ(sink.blocks.size(), 2u);
    EXPECT_EQ(asText(sink.blocks[0]), "aaaaabbbbb");
    EXPECT_EQ(asText(sink.blocks[1]), "ccccc");
}

// A large file sharing a solid block would force a partial restore to
// decompress the whole block just to reach a neighbour.
TEST(BlockPacker, GivesALargeFileItsOwnBlock) {
    RecordingSink sink;
    BlockPacker packer(16, sink.asSink());

    const auto small = packer.add(bytesOf("small"));
    const auto large = packer.add(bytesOf(std::string(64, 'L')));
    ASSERT_TRUE(small);
    ASSERT_TRUE(large);
    ASSERT_TRUE(packer.flush());

    ASSERT_EQ(sink.blocks.size(), 2u);
    EXPECT_EQ(asText(sink.blocks[0]), "small") << "the buffered block is flushed first";
    EXPECT_EQ(sink.blocks[1].size(), 64u);

    const auto largeLocation = packer.location(*large);
    ASSERT_TRUE(largeLocation);
    EXPECT_EQ(largeLocation->offset, 0u);
    EXPECT_EQ(largeLocation->length, 64u);
}

TEST(BlockPacker, StoresIdenticalContentOnlyOnce) {
    RecordingSink sink;
    BlockPacker packer(1024, sink.asSink());

    const auto first = packer.add(bytesOf("duplicated payload"));
    const auto second = packer.add(bytesOf("duplicated payload"));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(packer.flush());

    ASSERT_EQ(sink.blocks.size(), 1u);
    EXPECT_EQ(asText(sink.blocks[0]), "duplicated payload") << "stored once, not twice";

    EXPECT_FALSE(packer.isDeduplicated(*first));
    EXPECT_TRUE(packer.isDeduplicated(*second));
    EXPECT_EQ(packer.deduplicatedBytes(), 18u);

    const auto firstLocation = packer.location(*first);
    const auto secondLocation = packer.location(*second);
    ASSERT_TRUE(firstLocation);
    ASSERT_TRUE(secondLocation);
    EXPECT_EQ(firstLocation->blockId, secondLocation->blockId);
    EXPECT_EQ(firstLocation->offset, secondLocation->offset);
}

TEST(BlockPacker, DeduplicatesAcrossBlockBoundaries) {
    RecordingSink sink;
    BlockPacker packer(10, sink.asSink());

    const auto first = packer.add(bytesOf("aaaaa"));
    ASSERT_TRUE(packer.add(bytesOf("bbbbb")));
    ASSERT_TRUE(packer.add(bytesOf("ccccc")));  // forces a flush
    const auto repeat = packer.add(bytesOf("aaaaa"));
    ASSERT_TRUE(first);
    ASSERT_TRUE(repeat);
    ASSERT_TRUE(packer.flush());

    EXPECT_TRUE(packer.isDeduplicated(*repeat));
    EXPECT_EQ(*packer.location(*first), *packer.location(*repeat));
}

TEST(BlockPacker, EmptyContentNeedsNoBlock) {
    RecordingSink sink;
    BlockPacker packer(1024, sink.asSink());

    const auto empty = packer.add(ByteView{});
    ASSERT_TRUE(empty);
    ASSERT_TRUE(packer.flush());

    EXPECT_TRUE(sink.blocks.empty());
    const auto location = packer.location(*empty);
    ASSERT_TRUE(location);
    EXPECT_EQ(location->length, 0u);
}

TEST(BlockPacker, RefusesToReportALocationBeforeItIsFlushed) {
    RecordingSink sink;
    BlockPacker packer(1024, sink.asSink());

    const auto handle = packer.add(bytesOf("pending"));
    ASSERT_TRUE(handle);

    const auto location = packer.location(*handle);
    ASSERT_FALSE(location) << "a buffered placement has no block id yet";
    EXPECT_EQ(location.error().code, ErrorCode::Internal);
}

}  // namespace
}  // namespace transmit::format
