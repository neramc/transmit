#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "format/FileIo.h"

namespace transmit::format {
namespace {

class FileIoTest : public testing::Test {
protected:
    void SetUp() override {
        directory_ = std::filesystem::temp_directory_path() /
                     ("transmit-fileio-" + std::to_string(counter_++));
        std::filesystem::remove_all(directory_);
        std::filesystem::create_directories(directory_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(directory_, ec);
    }

    [[nodiscard]] std::string contentsOf(const std::filesystem::path& path) const {
        std::ifstream stream(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), {});
    }

    std::filesystem::path directory_;
    static inline int counter_ = 0;
};

ByteBuffer textBytes(std::string_view text) {
    const auto view = asBytes(text);
    return ByteBuffer(view.begin(), view.end());
}

TEST_F(FileIoTest, SyncsWhatWasWritten) {
    const auto path = directory_ / "synced";
    auto stream = FileStream::open(path, FileStream::Mode::Write);
    ASSERT_TRUE(stream) << stream.error().toString();

    const ByteBuffer payload = textBytes("the bytes have to reach the device");
    ASSERT_TRUE(stream->write(payload));
    const auto synced = stream->sync();
    ASSERT_TRUE(synced) << synced.error().toString();

    // A sync is not a close: writing has to carry on afterwards.
    ASSERT_TRUE(stream->write(textBytes(", twice")));
    ASSERT_TRUE(stream->sync());
    stream->close();

    EXPECT_EQ(contentsOf(path), "the bytes have to reach the device, twice");
}

TEST_F(FileIoTest, SyncingAClosedStreamIsNotAnError) {
    FileStream stream;
    EXPECT_TRUE(stream.sync());
}

TEST_F(FileIoTest, SyncsADirectory) {
    const auto synced = syncDirectory(directory_);
    EXPECT_TRUE(synced) << synced.error().toString();
}

TEST_F(FileIoTest, RefusesToSyncADirectoryThatIsNotThere) {
#if !defined(_WIN32)
    // Windows has no directory handle to flush, so syncDirectory is a no-op
    // there and has nothing to report.
    const auto synced = syncDirectory(directory_ / "no-such-folder");
    ASSERT_FALSE(synced);
    EXPECT_EQ(synced.error().code, ErrorCode::NotFound);
#else
    GTEST_SKIP() << "syncDirectory does nothing on Windows";
#endif
}

TEST_F(FileIoTest, WritesAtomicallyAtEveryDurability) {
    for (const Durability durability :
         {Durability::Buffered, Durability::Data, Durability::DataAndName}) {
        const auto path = directory_ / ("atomic-" + std::to_string(static_cast<int>(durability)));
        ASSERT_TRUE(writeFileAtomically(path, textBytes("first"), durability));
        EXPECT_EQ(contentsOf(path), "first");

        // Over the top of an existing file, which is the case that matters:
        // the reader must see one version or the other, never a mixture.
        ASSERT_TRUE(writeFileAtomically(path, textBytes("second, longer"), durability));
        EXPECT_EQ(contentsOf(path), "second, longer");
    }
}

TEST_F(FileIoTest, LeavesNoTemporaryFileWhenTheWriteFails) {
    // A directory cannot be opened for writing, so the temporary file cannot
    // be created and the target must be untouched either way.
    const auto path = directory_ / "subdir";
    std::filesystem::create_directories(path);
    EXPECT_FALSE(writeFileAtomically(path, textBytes("nope")));

    // And with a target whose parent does not exist at all.
    const auto missing = directory_ / "absent" / "file";
    EXPECT_FALSE(writeFileAtomically(missing, textBytes("nope")));
    EXPECT_FALSE(std::filesystem::exists(missing.parent_path()));
}

TEST_F(FileIoTest, KeepsTheOriginalWhenTheReplacementCannotBeWritten) {
    const auto path = directory_ / "kept";
    ASSERT_TRUE(writeFileAtomically(path, textBytes("original")));

    // Occupy the temporary name with a directory, which cannot be opened as a
    // file, so the write fails before the rename.
    std::filesystem::path temporary = path;
    temporary += ".transmit-tmp";
    std::filesystem::create_directories(temporary);

    EXPECT_FALSE(writeFileAtomically(path, textBytes("replacement")));
    EXPECT_EQ(contentsOf(path), "original");

    std::filesystem::remove_all(temporary);
}

}  // namespace
}  // namespace transmit::format
