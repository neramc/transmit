// What the journal has to survive.
//
// The journal exists for one situation: the machine stopped part way through a
// capture. That means every test here is about a file that was being written
// when the writing stopped, and the thing being checked is always the same -
// that what comes back out is a prefix of what went in, never a guess at the
// rest. A journal that returns one record too many sends a resume past the end
// of the data on the drive, and the archive it finishes is quietly wrong.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "format/Manifest.h"
#include "format/TransferJournal.h"

#include "support/TempDirectory.h"

namespace transmit::format {
namespace {

class TransferJournalTest : public testing::Test {
protected:
    void SetUp() override { directory_ = test_support::makeTemporaryDirectory("transmit-journal"); }

    void TearDown() override { test_support::removeTemporaryDirectory(directory_); }

    [[nodiscard]] std::filesystem::path archivePath() const { return directory_ / "capture.txa"; }
    [[nodiscard]] std::filesystem::path journalPath() const {
        return TransferJournal::pathFor(archivePath());
    }

    static JournalFingerprint fingerprint() {
        JournalFingerprint print;
        print.archiveUuid.fill(Byte{0x5A});
        print.destination = "/media/usb/capture.txa";
        print.hostName = "workshop";
        print.userName = "ada";
        print.optionsDigest = 0x0123456789ABCDEFULL;
        print.sourceDigest = 0xFEDCBA9876543210ULL;
        return print;
    }

    static JournalBlock block(std::uint32_t id, std::uint64_t end) {
        JournalBlock written;
        written.blockId = id;
        written.logicalEnd = end;
        written.rawSize = 64 * 1024;
        written.rawHash.fill(static_cast<Byte>(id));
        return written;
    }

    static ManifestEntry entry(std::uint64_t id, const std::string& name, std::uint32_t blockId) {
        ManifestEntry made;
        made.id = id;
        made.domain = DomainId::UserData;
        made.type = EntryType::File;
        made.path = TokenizedPath{PathTokenId::Documents, name};
        made.size = 4096;
        made.modifiedUnixNs = 1'700'000'000'000'000'000LL;
        made.contentHash.fill(static_cast<Byte>(id));
        made.location = BlockLocation{blockId, 0, 4096};
        made.appId = "org.example.app";
        return made;
    }

    /// Cuts the file to `length`, which is what an interrupted append leaves.
    void truncateJournal(std::uint64_t length) const {
        std::filesystem::resize_file(journalPath(), length);
    }

    [[nodiscard]] std::uint64_t journalSize() const {
        return std::filesystem::file_size(journalPath());
    }

    /// Flips one bit, for the case where the drive kept the length and lost
    /// the payload - which the checksum, not the length, has to catch.
    void flipBit(std::uint64_t offset) const {
        std::fstream file(journalPath(), std::ios::binary | std::ios::in | std::ios::out);
        file.seekg(static_cast<std::streamoff>(offset));
        char value = 0;
        file.read(&value, 1);
        value = static_cast<char>(value ^ 0x01);
        file.seekp(static_cast<std::streamoff>(offset));
        file.write(&value, 1);
    }

    std::filesystem::path directory_;
};

TEST_F(TransferJournalTest, ARecordedCaptureComesBackExactlyAsItWasWritten) {
    {
        auto journal = TransferJournal::begin(archivePath(), fingerprint());
        ASSERT_TRUE(journal) << journal.error().toString();
        ASSERT_TRUE((*journal)->recordBlock(block(1, 4096)));
        ASSERT_TRUE((*journal)->recordEntry(entry(1, "notes.txt", 1)));
        ASSERT_TRUE((*journal)->recordEntry(entry(2, "budget.ods", 1)));
        ASSERT_TRUE((*journal)->recordBlock(block(2, 9000)));
        ASSERT_TRUE((*journal)->recordEntry(entry(3, "photo.jpg", 2)));
        ASSERT_TRUE((*journal)->close());
    }

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents) << contents.error().toString();

    EXPECT_EQ(contents->fingerprint, fingerprint());
    EXPECT_FALSE(contents->complete);
    EXPECT_FALSE(contents->tailDiscarded);

    ASSERT_EQ(contents->blocks.size(), 2u);
    EXPECT_EQ(contents->blocks[0].blockId, 1u);
    EXPECT_EQ(contents->blocks[0].logicalEnd, 4096u);
    EXPECT_EQ(contents->blocks[1].blockId, 2u);
    EXPECT_EQ(contents->blocks[1].rawHash, block(2, 0).rawHash);
    EXPECT_EQ(contents->resumableLength(), 9000u);

    ASSERT_EQ(contents->entries.size(), 3u);
    EXPECT_EQ(contents->entries[0].path.relative, "notes.txt");
    EXPECT_EQ(contents->entries[2].location.blockId, 2u);
    EXPECT_EQ(contents->entries[2].appId, "org.example.app");
    EXPECT_EQ(contents->validBytes, journalSize());
}

TEST_F(TransferJournalTest, AFinishedCaptureSaysSoAndIsNotAResumePoint) {
    auto journal = TransferJournal::begin(archivePath(), fingerprint());
    ASSERT_TRUE(journal);
    ASSERT_TRUE((*journal)->recordBlock(block(1, 4096)));
    ASSERT_TRUE((*journal)->recordComplete());
    ASSERT_TRUE((*journal)->close());

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents);
    EXPECT_TRUE(contents->complete);
}

// The whole reason for the format. Every prefix of a journal has to read as
// the records that are wholly present and nothing else, because that is what
// the file looks like when the power goes: the last write is however far it
// got. Testing one truncation would prove nothing - the interesting lengths
// are the ones in the middle of a length field, in the middle of a checksum,
// and one byte short of a payload - so every prefix is tried.
TEST_F(TransferJournalTest, EveryPossibleTornTailReadsAsTheRecordsBeforeIt) {
    {
        auto journal = TransferJournal::begin(archivePath(), fingerprint());
        ASSERT_TRUE(journal);
        ASSERT_TRUE((*journal)->recordBlock(block(1, 1000)));
        ASSERT_TRUE((*journal)->recordEntry(entry(1, "a.txt", 1)));
        ASSERT_TRUE((*journal)->recordBlock(block(2, 2000)));
        ASSERT_TRUE((*journal)->recordEntry(entry(2, "b.txt", 2)));
        ASSERT_TRUE((*journal)->recordBlock(block(3, 3000)));
        ASSERT_TRUE((*journal)->close());
    }

    std::vector<char> whole;
    {
        std::ifstream file(journalPath(), std::ios::binary);
        whole.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    const auto full = readTransferJournal(archivePath());
    ASSERT_TRUE(full);
    const std::uint64_t completeSize = journalSize();

    std::uint64_t lastBlockCount = 0;
    std::uint64_t lastEntryCount = 0;

    for (std::uint64_t length = 0; length <= completeSize; ++length) {
        {
            std::ofstream file(journalPath(), std::ios::binary | std::ios::trunc);
            file.write(whole.data(), static_cast<std::streamsize>(length));
        }

        const auto contents = readTransferJournal(archivePath());

        // Until the whole session-start record is there, there is nothing to
        // resume into and the journal says so rather than guessing.
        if (!contents) {
            EXPECT_LT(length, completeSize) << "the complete journal must read";
            continue;
        }

        // Whatever survives is a prefix: counts never go backwards as the file
        // gets longer, and never exceed what the whole file holds.
        EXPECT_GE(contents->blocks.size(), lastBlockCount) << "at length " << length;
        EXPECT_GE(contents->entries.size(), lastEntryCount) << "at length " << length;
        EXPECT_LE(contents->blocks.size(), full->blocks.size()) << "at length " << length;
        EXPECT_LE(contents->entries.size(), full->entries.size()) << "at length " << length;
        lastBlockCount = contents->blocks.size();
        lastEntryCount = contents->entries.size();

        // Every record it did return is byte-identical to the original.
        for (std::size_t i = 0; i < contents->blocks.size(); ++i) {
            EXPECT_EQ(contents->blocks[i].blockId, full->blocks[i].blockId);
            EXPECT_EQ(contents->blocks[i].logicalEnd, full->blocks[i].logicalEnd);
        }
        for (std::size_t i = 0; i < contents->entries.size(); ++i) {
            EXPECT_EQ(contents->entries[i].path.relative, full->entries[i].path.relative);
            EXPECT_EQ(contents->entries[i].location, full->entries[i].location);
        }

        // The surviving length is a record boundary, so appending after it
        // cannot land behind a half-written record.
        EXPECT_LE(contents->validBytes, length) << "at length " << length;
        EXPECT_EQ(contents->tailDiscarded, contents->validBytes != length)
            << "at length " << length;
    }

    EXPECT_EQ(lastBlockCount, full->blocks.size());
    EXPECT_EQ(lastEntryCount, full->entries.size());
}

// A stick that acknowledged the write and lost the bytes leaves the length
// intact and the payload wrong, which no amount of length checking finds.
TEST_F(TransferJournalTest, ARecordWhoseBytesChangedIsDroppedAlongWithEverythingAfterIt) {
    std::uint64_t afterFirstBlock = 0;
    {
        auto journal = TransferJournal::begin(archivePath(), fingerprint());
        ASSERT_TRUE(journal);
        ASSERT_TRUE((*journal)->recordBlock(block(1, 1000)));
        afterFirstBlock = (*journal)->bytesWritten();
        ASSERT_TRUE((*journal)->recordBlock(block(2, 2000)));
        ASSERT_TRUE((*journal)->recordEntry(entry(1, "a.txt", 2)));
        ASSERT_TRUE((*journal)->close());
    }

    // Land in the middle of the second block record's payload.
    flipBit(afterFirstBlock + 10);

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents) << contents.error().toString();
    EXPECT_TRUE(contents->tailDiscarded);
    ASSERT_EQ(contents->blocks.size(), 1u);
    EXPECT_EQ(contents->blocks[0].blockId, 1u);
    // The entry after the damaged record is dropped too. It described a block
    // whose record can no longer be trusted, and keeping it would put an entry
    // in the manifest pointing into bytes nobody has checked.
    EXPECT_TRUE(contents->entries.empty());
    EXPECT_EQ(contents->validBytes, afterFirstBlock);
}

TEST_F(TransferJournalTest, AJournalCarriedOnFromIsTrimmedToWhatWasRead) {
    std::uint64_t afterFirstBlock = 0;
    {
        auto journal = TransferJournal::begin(archivePath(), fingerprint());
        ASSERT_TRUE(journal);
        ASSERT_TRUE((*journal)->recordBlock(block(1, 1000)));
        afterFirstBlock = (*journal)->bytesWritten();
        ASSERT_TRUE((*journal)->recordBlock(block(2, 2000)));
        ASSERT_TRUE((*journal)->close());
    }

    // Cut half of the second record off, as an interrupted append would.
    truncateJournal(afterFirstBlock + 4);

    const auto first = readTransferJournal(archivePath());
    ASSERT_TRUE(first);
    ASSERT_EQ(first->blocks.size(), 1u);
    EXPECT_TRUE(first->tailDiscarded);

    {
        auto journal = TransferJournal::reopen(archivePath(), first->validBytes);
        ASSERT_TRUE(journal) << journal.error().toString();
        ASSERT_TRUE((*journal)->recordBlock(block(7, 7000)));
        ASSERT_TRUE((*journal)->close());
    }

    const auto second = readTransferJournal(archivePath());
    ASSERT_TRUE(second);
    EXPECT_FALSE(second->tailDiscarded) << "the trimmed tail came back";
    ASSERT_EQ(second->blocks.size(), 2u);
    EXPECT_EQ(second->blocks[0].blockId, 1u);
    EXPECT_EQ(second->blocks[1].blockId, 7u);
    EXPECT_EQ(second->fingerprint, fingerprint()) << "reopening lost the fingerprint";
}

// Carrying on from a point *before* the end of the journal is the case that
// matters, and it is not the torn-tail one: when the archive on the drive
// turns out to be shorter than the journal claims, records that are perfectly
// valid have to be thrown away because the blocks they describe are not there
// any more. If they are merely written over rather than cut off, a shorter
// replacement leaves the tail of an old record intact behind it - and an old
// record still has its own valid checksum, so the next read hands back an
// entry pointing into bytes the archive no longer holds.
TEST_F(TransferJournalTest, RecordsPastTheResumePointCannotComeBackBehindTheNewOnes) {
    std::uint64_t afterFirstBlock = 0;
    {
        auto journal = TransferJournal::begin(archivePath(), fingerprint());
        ASSERT_TRUE(journal);
        ASSERT_TRUE((*journal)->recordBlock(block(1, 1000)));
        afterFirstBlock = (*journal)->bytesWritten();

        // Long records, so that a short one appended over them cannot bury
        // them by accident.
        ASSERT_TRUE((*journal)->recordEntry(
            entry(1, std::string(400, 'a') + "/deep/and/long/name.txt", 2)));
        ASSERT_TRUE((*journal)->recordEntry(
            entry(2, std::string(400, 'b') + "/deep/and/long/other.txt", 2)));
        ASSERT_TRUE((*journal)->recordBlock(block(2, 2000)));
        ASSERT_TRUE((*journal)->close());
    }

    const auto before = readTransferJournal(archivePath());
    ASSERT_TRUE(before);
    ASSERT_EQ(before->entries.size(), 2u) << "the fixture did not write what the test needs";
    ASSERT_GT(journalSize(), afterFirstBlock + 100) << "the discarded tail is too short to matter";

    // The drive turned out to hold only the first block, so everything after
    // it is dropped even though every record of it is intact.
    {
        auto journal = TransferJournal::reopen(archivePath(), afterFirstBlock);
        ASSERT_TRUE(journal) << journal.error().toString();
        ASSERT_TRUE((*journal)->recordBlock(block(9, 1500)));
        ASSERT_TRUE((*journal)->close());
    }

    const auto after = readTransferJournal(archivePath());
    ASSERT_TRUE(after) << after.error().toString();
    EXPECT_TRUE(after->entries.empty()) << "an entry survived the resume point";
    ASSERT_EQ(after->blocks.size(), 2u);
    EXPECT_EQ(after->blocks[0].blockId, 1u);
    EXPECT_EQ(after->blocks[1].blockId, 9u);
    EXPECT_FALSE(after->tailDiscarded) << "something was left behind the new record";
    EXPECT_EQ(after->validBytes, journalSize());
}

// A capture compresses on several threads, so a block's id is handed out - and
// the entries going into it settle on their locations - while the block itself
// is still queued behind others. An entry can therefore reach the journal
// before its block does, and if the run stops in between, that entry describes
// bytes that are not on the drive. Following it would put a location in the
// manifest pointing at nothing, which is far worse than losing the file.
TEST_F(TransferJournalTest, AnEntryWhoseBlockNeverReachedTheJournalIsNotReturned) {
    {
        auto journal = TransferJournal::begin(archivePath(), fingerprint());
        ASSERT_TRUE(journal);
        ASSERT_TRUE((*journal)->recordBlock(block(1, 1000)));
        ASSERT_TRUE((*journal)->recordEntry(entry(1, "landed.txt", 1)));
        // Block 2 was queued and its entry settled, but the run stopped before
        // the block was written.
        ASSERT_TRUE((*journal)->recordEntry(entry(2, "still-queued.txt", 2)));
        ASSERT_TRUE((*journal)->close());
    }

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents) << contents.error().toString();
    EXPECT_FALSE(contents->tailDiscarded) << "the journal itself is intact";
    ASSERT_EQ(contents->entries.size(), 1U);
    EXPECT_EQ(contents->entries[0].path.relative, "landed.txt");
}

// A directory, a symbolic link and an empty file point at no block at all.
// There is nothing on the drive for them to be missing, so the rule above must
// not take them with it.
TEST_F(TransferJournalTest, AnEntryWithNothingInItNeedsNoBlock) {
    {
        auto journal = TransferJournal::begin(archivePath(), fingerprint());
        ASSERT_TRUE(journal);

        ManifestEntry folder = entry(1, "Projects", 0);
        folder.type = EntryType::Directory;
        folder.size = 0;
        folder.location = BlockLocation{0, 0, 0};
        ASSERT_TRUE((*journal)->recordEntry(folder));

        ManifestEntry empty = entry(2, "empty.txt", 0);
        empty.size = 0;
        empty.location = BlockLocation{0, 0, 0};
        ASSERT_TRUE((*journal)->recordEntry(empty));
        ASSERT_TRUE((*journal)->close());
    }

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents) << contents.error().toString();
    EXPECT_TRUE(contents->blocks.empty());
    EXPECT_EQ(contents->entries.size(), 2U) << "an entry with no content was dropped";
}

// The journal is synced in front of the data it describes, so it can outlive
// it: the record of a block reaches the drive and the block does not. What is
// really there decides, not what the journal remembers.
TEST_F(TransferJournalTest, WhatTheDriveDidNotKeepIsDropped) {
    {
        auto journal = TransferJournal::begin(archivePath(), fingerprint());
        ASSERT_TRUE(journal);
        ASSERT_TRUE((*journal)->recordBlock(block(1, 1000)));
        ASSERT_TRUE((*journal)->recordEntry(entry(1, "safe.txt", 1)));
        ASSERT_TRUE((*journal)->recordBlock(block(2, 2000)));
        ASSERT_TRUE((*journal)->recordEntry(entry(2, "lost.txt", 2)));
        ASSERT_TRUE((*journal)->close());
    }

    auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents);
    ASSERT_EQ(contents->blocks.size(), 2U);
    ASSERT_EQ(contents->entries.size(), 2U);

    // The archive really stops at 1500 bytes: block 2 is only half there.
    contents->keepOnlyWhatFitsIn(1500);

    ASSERT_EQ(contents->blocks.size(), 1U);
    EXPECT_EQ(contents->blocks[0].blockId, 1U);
    EXPECT_EQ(contents->resumableLength(), 1000U);
    ASSERT_EQ(contents->entries.size(), 1U);
    EXPECT_EQ(contents->entries[0].path.relative, "safe.txt");
}

TEST_F(TransferJournalTest, ABlockComesBackAsTheManifestRecordsIt) {
    JournalBlock written;
    written.blockId = 4;
    written.streamOffset = 4096;
    written.logicalEnd = 9000;
    written.rawSize = 70000;
    written.storedSize = 4856;
    written.codec = CodecId::Zstd;
    written.encrypted = true;
    written.rawHash.fill(Byte{0x11});

    {
        auto journal = TransferJournal::begin(archivePath(), fingerprint());
        ASSERT_TRUE(journal);
        ASSERT_TRUE((*journal)->recordBlock(written));
        ASSERT_TRUE((*journal)->close());
    }

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents) << contents.error().toString();
    ASSERT_EQ(contents->blocks.size(), 1U);

    const BlockRecord record = contents->blocks[0].asBlockRecord();
    EXPECT_EQ(record.blockId, 4U);
    EXPECT_EQ(record.streamOffset, 4096U);
    EXPECT_EQ(record.rawSize, 70000U);
    EXPECT_EQ(record.storedSize, 4856U);
    EXPECT_EQ(record.codec, CodecId::Zstd);
    EXPECT_TRUE(record.encrypted);
}

// The journal is an append-only log and a resumed capture really does write
// about the same file twice: the first run records an entry, the block it went
// into never reaches the drive, so the resume drops that entry, captures the
// file again and appends a second record. The first record is still in the
// file. Believing both would put the same path in the manifest twice, and a
// restore would then write it twice or refuse outright.
//
// This was found by running the command line against a drive that filled up,
// not by reading the code: the capture reported "61 files" out of sixty.
TEST_F(TransferJournalTest, TheLastRecordOfAnEntryIsTheOneThatCounts) {
    {
        auto journal = TransferJournal::begin(archivePath(), fingerprint());
        ASSERT_TRUE(journal);
        ASSERT_TRUE((*journal)->recordBlock(block(1, 1000)));

        // The first run's version, pointing into a block that did land.
        ASSERT_TRUE((*journal)->recordEntry(entry(1, "twice.txt", 1)));

        // The resumed run's version of the same file, in a later block.
        ASSERT_TRUE((*journal)->recordBlock(block(2, 2000)));
        ManifestEntry again = entry(7, "twice.txt", 2);
        again.location = BlockLocation{2, 512, 4096};
        ASSERT_TRUE((*journal)->recordEntry(again));

        ASSERT_TRUE((*journal)->recordEntry(entry(8, "once.txt", 2)));
        ASSERT_TRUE((*journal)->close());
    }

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents) << contents.error().toString();
    ASSERT_EQ(contents->entries.size(), 2U) << "the same file came back twice";

    const auto twice =
        std::find_if(contents->entries.begin(), contents->entries.end(),
                     [](const ManifestEntry& e) { return e.path.relative == "twice.txt"; });
    ASSERT_NE(twice, contents->entries.end());
    EXPECT_EQ(twice->id, 7U) << "the earlier record won";
    EXPECT_EQ(twice->location.blockId, 2U);
    EXPECT_EQ(twice->location.offset, 512U);
}

TEST_F(TransferJournalTest, AJournalWithNoSessionStartIsRefusedRatherThanResumedFromZero) {
    auto journal = TransferJournal::begin(archivePath(), fingerprint());
    ASSERT_TRUE(journal);
    ASSERT_TRUE((*journal)->close());

    // Keep the file header, lose the session-start record.
    truncateJournal(16);

    const auto contents = readTransferJournal(archivePath());
    EXPECT_FALSE(contents);
    EXPECT_EQ(contents.error().code, ErrorCode::CorruptArchive);
}

TEST_F(TransferJournalTest, SomethingThatIsNotAJournalIsNotReadAsAnEmptyOne) {
    {
        std::ofstream file(journalPath(), std::ios::binary);
        const std::string text = "this is a text file that happens to sit beside an archive";
        file << text;
    }

    const auto contents = readTransferJournal(archivePath());
    EXPECT_FALSE(contents);
    EXPECT_EQ(contents.error().code, ErrorCode::CorruptArchive);
}

TEST_F(TransferJournalTest, AMissingJournalIsNotFoundRatherThanCorrupt) {
    const auto contents = readTransferJournal(archivePath());
    EXPECT_FALSE(contents);
    EXPECT_EQ(contents.error().code, ErrorCode::NotFound)
        << "no journal means nothing to resume, not a damaged archive";
}

TEST_F(TransferJournalTest, TheJournalSitsBesideTheArchiveAndIsRemovedWithIt) {
    auto journal = TransferJournal::begin(archivePath(), fingerprint());
    ASSERT_TRUE(journal);
    ASSERT_TRUE((*journal)->close());

    EXPECT_EQ(journalPath().filename().string(), "capture.txa.journal");
    EXPECT_TRUE(std::filesystem::exists(journalPath()));

    ASSERT_TRUE(TransferJournal::discard(archivePath()));
    EXPECT_FALSE(std::filesystem::exists(journalPath()));

    // Removing one that is already gone is not a failure: a capture that
    // finished twice - retried after an error at the very end - must not fail
    // on the tidying up.
    EXPECT_TRUE(TransferJournal::discard(archivePath()));
}

TEST_F(TransferJournalTest, TheDigestSeparatesThingsThatMustNotBeConfused) {
    const auto one = journalDigest(asBytes("/home/ada/photos"));
    const auto two = journalDigest(asBytes("/home/ada/photo"));
    EXPECT_NE(one, two);
    EXPECT_EQ(one, journalDigest(asBytes("/home/ada/photos"))) << "the digest is not stable";
    EXPECT_NE(journalDigest(asBytes("")), 0u);
}

}  // namespace
}  // namespace transmit::format
