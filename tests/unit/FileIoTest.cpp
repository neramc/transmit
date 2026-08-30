#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "format/FileIo.h"

#include "support/TempDirectory.h"

namespace transmit::format {
namespace {

class FileIoTest : public testing::Test {
protected:
    void SetUp() override { directory_ = test_support::makeTemporaryDirectory("transmit-fileio"); }

    void TearDown() override { test_support::removeTemporaryDirectory(directory_); }

    [[nodiscard]] std::string contentsOf(const std::filesystem::path& path) const {
        std::ifstream stream(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), {});
    }

    std::filesystem::path directory_;
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

TEST_F(FileIoTest, RetriesOnlyWhatAnotherAttemptCouldFix) {
    // A full disk, a file that is not ours, an unplugged stick: answering
    // again costs the user time and changes nothing.
    Error full(ErrorCode::IoError, "no space");
    full.systemCode = ENOSPC;
    EXPECT_FALSE(isTransient(full));

    Error denied(ErrorCode::PermissionDenied, "not yours");
    denied.systemCode = EACCES;
    EXPECT_FALSE(isTransient(denied));

    Error gone(ErrorCode::IoError, "unplugged");
    gone.systemCode = ENODEV;
    EXPECT_FALSE(isTransient(gone));

    // A bad read off marginal media often works on the next pass.
    Error flaky(ErrorCode::IoError, "one bad read");
    flaky.systemCode = EIO;
    EXPECT_TRUE(isTransient(flaky));

    // Bytes that are already wrong stay wrong however many times they are
    // read, and a cancellation is a decision rather than a fault.
    EXPECT_FALSE(isTransient(Error(ErrorCode::CorruptArchive)));
    EXPECT_FALSE(isTransient(Error(ErrorCode::IntegrityMismatch)));
    EXPECT_FALSE(isTransient(Error(ErrorCode::Cancelled)));
    EXPECT_FALSE(isTransient(Error(ErrorCode::EndOfStream)));

    // And a failure the system had no hand in is not guesswork material.
    EXPECT_FALSE(isTransient(Error(ErrorCode::Internal)));
}

TEST_F(FileIoTest, StopsRetryingOnceItSucceeds) {
    int calls = 0;
    RetryPolicy policy;
    policy.attempts = 4;
    policy.firstDelay = std::chrono::milliseconds{0};

    const Status result = withRetry(policy, [&calls]() -> Status {
        ++calls;
        if (calls < 3) {
            Error flaky(ErrorCode::IoError, "bad read");
            flaky.systemCode = EIO;
            return flaky;
        }
        return ok();
    });

    EXPECT_TRUE(result);
    EXPECT_EQ(calls, 3);
}

TEST_F(FileIoTest, GivesUpAfterTheLastAttempt) {
    int calls = 0;
    RetryPolicy policy;
    policy.attempts = 3;
    policy.firstDelay = std::chrono::milliseconds{0};

    const Status result = withRetry(policy, [&calls]() -> Status {
        ++calls;
        Error flaky(ErrorCode::IoError, "bad read every time");
        flaky.systemCode = EIO;
        return flaky;
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().systemCode, EIO);
    EXPECT_EQ(calls, 3);
}

TEST_F(FileIoTest, DoesNotRetryWhatCannotBeFixed) {
    int calls = 0;
    RetryPolicy policy;
    policy.attempts = 5;
    policy.firstDelay = std::chrono::milliseconds{0};

    const Status result = withRetry(policy, [&calls]() -> Status {
        ++calls;
        Error full(ErrorCode::IoError, "the disk is full");
        full.systemCode = ENOSPC;
        return full;
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(calls, 1) << "a full disk was asked five times whether it was still full";
}

TEST_F(FileIoTest, RetryPolicyOnceMeansOnce) {
    int calls = 0;
    const Status result = withRetry(RetryPolicy::once(), [&calls]() -> Status {
        ++calls;
        Error flaky(ErrorCode::IoError, "bad read");
        flaky.systemCode = EIO;
        return flaky;
    });

    EXPECT_FALSE(result);
    EXPECT_EQ(calls, 1);
}

}  // namespace
}  // namespace transmit::format
