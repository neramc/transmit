#include <algorithm>
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

// ---------------------------------------------------------------- holes
//
// A hole and a run of zeroes are the same bytes and different amounts of disk.
// That is the whole feature, and it is also what makes it awkward to test:
// every assertion about the content has to pass whatever the filesystem did,
// and only the assertions about the space are allowed to depend on it.

TEST_F(FileIoTest, FindsNothingWorthWritingInAFileOfZeroes) {
    const ByteBuffer zeroes(4 * kSmallestHole);
    EXPECT_TRUE(spansWorthWriting(zeroes).empty());
}

TEST_F(FileIoTest, WritesAFileWithNoZeroesInOnePiece) {
    ByteBuffer dense(4 * kSmallestHole, Byte{0x5A});
    const auto spans = spansWorthWriting(dense);
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans.front(), (ByteRange{0, dense.size()}));
}

TEST_F(FileIoTest, KeepsAShortRunOfZeroesInsideTheSpan) {
    // Below the threshold a hole costs more than the zeroes do, and splitting
    // the span would turn one write into two for nothing.
    ByteBuffer data(4 * kSmallestHole, Byte{0x11});
    std::fill_n(data.begin() + static_cast<std::ptrdiff_t>(kSmallestHole), kSmallestHole - 1,
                Byte{0});

    const auto spans = spansWorthWriting(data);
    ASSERT_EQ(spans.size(), 1U);
    EXPECT_EQ(spans.front(), (ByteRange{0, data.size()}));
}

TEST_F(FileIoTest, SplitsTheSpanAroundALongRunOfZeroes) {
    ByteBuffer data(4 * kSmallestHole, Byte{0x11});
    std::fill_n(data.begin() + static_cast<std::ptrdiff_t>(kSmallestHole), 2 * kSmallestHole,
                Byte{0});

    const auto spans = spansWorthWriting(data);
    ASSERT_EQ(spans.size(), 2U);
    EXPECT_EQ(spans[0], (ByteRange{0, kSmallestHole}));
    EXPECT_EQ(spans[1], (ByteRange{3 * kSmallestHole, kSmallestHole}));
}

TEST_F(FileIoTest, HandlesAHoleAtEitherEnd) {
    ByteBuffer leading(3 * kSmallestHole, Byte{0});
    leading.back() = Byte{0x7F};
    const auto front = spansWorthWriting(leading);
    ASSERT_EQ(front.size(), 1U);
    EXPECT_EQ(front.front(), (ByteRange{3 * kSmallestHole - 1, 1}));

    // A file ending in zeroes has nothing written after its last span, which
    // is why the writer sets the length explicitly afterwards.
    ByteBuffer trailing(3 * kSmallestHole, Byte{0});
    trailing.front() = Byte{0x7F};
    const auto back = spansWorthWriting(trailing);
    ASSERT_EQ(back.size(), 1U);
    EXPECT_EQ(back.front(), (ByteRange{0, 1}));
}

TEST_F(FileIoTest, FindsNoSpansInNoBytes) {
    EXPECT_TRUE(spansWorthWriting(ByteView{}).empty());
}

TEST_F(FileIoTest, TreatsAHoleOfNoLengthAsOneByte) {
    // Asking for holes of zero length would divide the file into a span per
    // byte, so the smallest that means anything is used instead.
    ByteBuffer data(4, Byte{0x11});
    data[1] = Byte{0};

    const auto spans = spansWorthWriting(data, 0);
    ASSERT_EQ(spans.size(), 2U);
    EXPECT_EQ(spans[0], (ByteRange{0, 1}));
    EXPECT_EQ(spans[1], (ByteRange{2, 2}));
}

TEST_F(FileIoTest, AFileFullOfHolesReadsBackWhole) {
    // Four megabytes of which about a tenth is real, which is roughly the
    // shape of a disk image.
    constexpr std::size_t kLength = 4 * 1024 * 1024;
    ByteBuffer data(kLength, Byte{0});
    for (std::size_t at = 0; at < kLength; at += 512 * 1024) {
        std::fill_n(data.begin() + static_cast<std::ptrdiff_t>(at), 4096U, Byte{0xC3});
    }

    const auto path = directory_ / "mostly-hole";
    const auto written =
        writeFileAtomically(path, ByteView(data), Durability::Data, Sparseness::PunchHoles);
    ASSERT_TRUE(written) << written.error().toString();

    const auto readBack = readWholeFile(path);
    ASSERT_TRUE(readBack) << readBack.error().toString();
    EXPECT_EQ(*readBack, data) << "a hole has to read back as the zeroes it stands for";
    EXPECT_EQ(std::filesystem::file_size(path), kLength);

    // The same bytes written the ordinary way, as the control. Asking whether
    // the sparse file is small is only meaningful against a filesystem that
    // would otherwise have said it was large: a filesystem with no holes to
    // give answers the same for both, and that is not a failure of this code.
    // Comparing the two is what makes the assertion fire when the holes stop
    // being punched, rather than quietly passing everywhere.
    const auto control = directory_ / "written-out";
    ASSERT_TRUE(writeFileAtomically(control, ByteView(data), Durability::Data, Sparseness::Dense));

    const auto dense = allocatedSize(control);
    const auto occupied = allocatedSize(path);
    ASSERT_TRUE(dense) << dense.error().toString();
    ASSERT_TRUE(occupied) << occupied.error().toString();

    if (*dense >= kLength) {
        EXPECT_LT(*occupied, kLength / 2)
            << "the file is nine tenths hole and takes " << *occupied
            << " bytes of disk; written out in full the same bytes take " << *dense;
    } else {
        GTEST_SKIP() << "this filesystem does not account for holes: " << *dense
                     << " bytes allocated for " << kLength << " written in full";
    }
}

TEST_F(FileIoTest, GivesBackTheDiskUnderARegion) {
    // The half of the mechanism that macOS depends on entirely: APFS fills a
    // skipped region in, so there the zeroes are written and then handed back.
    // Exercised here on its own rather than only through the writer, because
    // on Linux the writer never needs it - the hole is already there - and a
    // path that only runs on one platform is a path nobody can test.
    constexpr std::uint64_t kLength = 2 * 1024 * 1024;
    const ByteBuffer data(kLength, Byte{0x4D});

    const auto path = directory_ / "punched";
    {
        auto stream = FileStream::open(path, FileStream::Mode::Write);
        ASSERT_TRUE(stream) << stream.error().toString();
        ASSERT_TRUE(stream->write(data));
        ASSERT_TRUE(stream->sync());
    }

    const auto before = allocatedSize(path);
    ASSERT_TRUE(before) << before.error().toString();

    {
        auto stream = FileStream::open(path, FileStream::Mode::ReadWrite);
        ASSERT_TRUE(stream) << stream.error().toString();
        const auto punched = stream->punchHole(kLength / 4, kLength / 2);
        EXPECT_TRUE(punched) << punched.error().toString();
    }

    // The length is unchanged and the hole reads as zeroes, whatever the
    // filesystem decided about the disk underneath.
    EXPECT_EQ(std::filesystem::file_size(path), kLength);
    const auto readBack = readWholeFile(path);
    ASSERT_TRUE(readBack) << readBack.error().toString();
    ASSERT_EQ(readBack->size(), kLength);
    EXPECT_EQ((*readBack)[kLength / 4], Byte{0});
    EXPECT_EQ((*readBack)[kLength / 4 + kLength / 2 - 1], Byte{0});
    EXPECT_EQ((*readBack)[0], Byte{0x4D});
    EXPECT_EQ((*readBack)[kLength - 1], Byte{0x4D});

    const auto after = allocatedSize(path);
    ASSERT_TRUE(after) << after.error().toString();
    if (*before >= kLength) {
        EXPECT_LT(*after, *before) << "half the file was given back and it takes the same room";
    }
}

TEST_F(FileIoTest, WhatSharesABlockWithAHoleIsLeftAlone) {
    // The edges of a region are the case the three systems disagree about:
    // macOS refuses an unaligned one outright, Linux and Windows accept it and
    // zero the bytes at its ends. Trimmed inward, all three leave whatever
    // else lives in those blocks exactly as it was.
    constexpr std::uint64_t kLength = 2 * 1024 * 1024;
    constexpr std::uint64_t kOffBy = 100;
    const ByteBuffer data(kLength, Byte{0x4D});

    const auto path = directory_ / "shared-block";
    {
        auto stream = FileStream::open(path, FileStream::Mode::Write);
        ASSERT_TRUE(stream) << stream.error().toString();
        ASSERT_TRUE(stream->write(data));
        ASSERT_TRUE(stream->sync());
    }
    {
        auto stream = FileStream::open(path, FileStream::Mode::ReadWrite);
        ASSERT_TRUE(stream) << stream.error().toString();
        ASSERT_TRUE(stream->punchHole(kLength / 4 + kOffBy, kLength / 2));
    }

    const auto readBack = readWholeFile(path);
    ASSERT_TRUE(readBack) << readBack.error().toString();
    ASSERT_EQ(readBack->size(), kLength);

    // The byte the caller named is inside a block that also holds data, so it
    // is untouched; well inside the region is a whole block and is gone.
    EXPECT_EQ((*readBack)[kLength / 4 + kOffBy], Byte{0x4D});
    EXPECT_EQ((*readBack)[kLength / 4 + kOffBy + kLength / 2 - 1], Byte{0x4D});
    EXPECT_EQ((*readBack)[kLength / 4 + 8192], Byte{0});
}

TEST_F(FileIoTest, PunchingNothingIsNotAFailure) {
    const auto path = directory_ / "not-punched";
    ASSERT_TRUE(writeFileAtomically(path, textBytes("short")));

    auto stream = FileStream::open(path, FileStream::Mode::ReadWrite);
    ASSERT_TRUE(stream) << stream.error().toString();
    // Nothing to give back, and a region smaller than one block: both are
    // ordinary, and neither may fail a restore.
    EXPECT_TRUE(stream->punchHole(0, 0));
    EXPECT_TRUE(stream->punchHole(1, 3));

    FileStream closed;
    EXPECT_TRUE(closed.punchHole(0, 4096));

    stream->close();
    EXPECT_EQ(contentsOf(path), "short");
}

TEST_F(FileIoTest, WritesASmallFileWholeEvenWhenAskedForHoles) {
    // Under the threshold the scan is skipped, so the file is written in one
    // piece - and still has to be right, which is the only part a caller can
    // see.
    ByteBuffer data(kSmallestSparseFile / 2, Byte{0});
    data.front() = Byte{0x2A};

    const auto path = directory_ / "small-and-empty";
    ASSERT_TRUE(
        writeFileAtomically(path, ByteView(data), Durability::Buffered, Sparseness::PunchHoles));

    const auto readBack = readWholeFile(path);
    ASSERT_TRUE(readBack) << readBack.error().toString();
    EXPECT_EQ(*readBack, data);
}

TEST_F(FileIoTest, SetsTheLengthWithoutWritingBytes) {
    const auto path = directory_ / "resized";
    auto stream = FileStream::open(path, FileStream::Mode::Write);
    ASSERT_TRUE(stream) << stream.error().toString();

    ASSERT_TRUE(stream->write(textBytes("nine bytes")));
    // Longer: the file grows with a hole, not with anything written.
    ASSERT_TRUE(stream->truncate(kSmallestHole));
    stream->close();
    EXPECT_EQ(std::filesystem::file_size(path), kSmallestHole);

    const auto readBack = readWholeFile(path);
    ASSERT_TRUE(readBack) << readBack.error().toString();
    EXPECT_EQ(std::count(readBack->begin(), readBack->end(), Byte{0}), kSmallestHole - 10);

    // And shorter, which is the direction that discards.
    auto again = FileStream::open(path, FileStream::Mode::ReadWrite);
    ASSERT_TRUE(again) << again.error().toString();
    ASSERT_TRUE(again->truncate(4));
    again->close();
    EXPECT_EQ(contentsOf(path), "nine");
}

TEST_F(FileIoTest, WillNotResizeOrMarkAFileThatIsNotOpen) {
    FileStream stream;
    EXPECT_FALSE(stream.truncate(0));
#if defined(_WIN32)
    EXPECT_FALSE(stream.declareSparse());
#else
    // Nowhere but Windows has to be asked: a write past the end of a file
    // makes the hole by itself, so there is nothing to fail.
    EXPECT_TRUE(stream.declareSparse());
#endif
}

TEST_F(FileIoTest, KnowsHowMuchDiskAFileTakes) {
    const auto path = directory_ / "measured";
    ASSERT_TRUE(writeFileAtomically(path, textBytes("a few bytes")));

    const auto occupied = allocatedSize(path);
    ASSERT_TRUE(occupied) << occupied.error().toString();
    // Rounded up to whole blocks, so a short file takes at least its length
    // and usually a good deal more.
    EXPECT_GE(*occupied, 0U);

    EXPECT_FALSE(allocatedSize(directory_ / "not-there"));
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
