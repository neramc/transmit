// What the restore journal has to survive, and what it must never claim.
//
// This journal answers one question for a restore that was interrupted: which
// items are already settled, and where did each of them land. Both halves
// matter, and they fail differently. Getting "which" wrong means a file the
// user asked for is silently never written. Getting "where" wrong means the
// resumed run invents a second name for a file it already saved under one, and
// the user ends up with two copies of something they have one of.
//
// So the tests come in those two shapes: what a torn file reads back as, and
// whether the names the first run chose come back intact.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "format/RestoreJournal.h"
#include "format/TransferJournal.h"

#include "support/TempDirectory.h"

namespace transmit::format {
namespace {

class RestoreJournalTest : public testing::Test {
protected:
    void SetUp() override {
        directory_ = test_support::makeTemporaryDirectory("transmit-restore-journal");
    }

    void TearDown() override { test_support::removeTemporaryDirectory(directory_); }

    [[nodiscard]] std::filesystem::path journalPath() const {
        return RestoreJournal::pathFor(directory_, uuid());
    }

    static ArchiveUuid uuid() {
        ArchiveUuid id{};
        id.fill(Byte{0x3C});
        return id;
    }

    static RestoreFingerprint fingerprint() {
        RestoreFingerprint print;
        print.archiveUuid = uuid();
        print.destination = "/home/ada";
        print.hostName = "workshop";
        print.userName = "ada";
        print.optionsDigest = 0x0123456789ABCDEFULL;
        print.rollbackArchivePath = "/home/ada/.transmit/undo-1.txa";
        return print;
    }

    static RestorePlacement placement(const std::string& source, const std::string& target,
                                      RestoreOutcome outcome = RestoreOutcome::Written) {
        RestorePlacement made;
        made.source = source;
        made.target = target;
        made.outcome = outcome;
        return made;
    }

    /// Writes a journal holding `count` placements and returns its length.
    std::uint64_t writeJournal(int count) {
        auto journal = RestoreJournal::begin(journalPath(), fingerprint());
        EXPECT_TRUE(journal.operator bool());
        for (int i = 0; i < count; ++i) {
            const std::string name = "file-" + std::to_string(i);
            EXPECT_TRUE((*journal)->recordPlacement(
                placement("{DOCUMENTS}/" + name, "/home/ada/Documents/" + name)));
        }
        EXPECT_TRUE((*journal)->close());
        return std::filesystem::file_size(journalPath());
    }

    /// Cuts the journal to `length` bytes, as an interrupted append leaves it.
    void truncateTo(std::uint64_t length) { std::filesystem::resize_file(journalPath(), length); }

    std::filesystem::path directory_;
};

TEST_F(RestoreJournalTest, RemembersWhatItWasToldInOrder) {
    auto journal = RestoreJournal::begin(journalPath(), fingerprint());
    ASSERT_TRUE(journal.operator bool());
    ASSERT_TRUE((*journal)->recordPlacement(placement("{DOCUMENTS}/a", "/home/ada/Documents/a")));
    ASSERT_TRUE((*journal)->recordPlacement(
        placement("{DOCUMENTS}/b", "/home/ada/Documents/b", RestoreOutcome::Skipped)));
    ASSERT_TRUE((*journal)->recordPlacement(
        placement("{DOCUMENTS}/c", "/home/ada/Documents/c", RestoreOutcome::Failed)));
    ASSERT_TRUE((*journal)->close());

    auto read = readRestoreJournal(journalPath());
    ASSERT_TRUE(read.operator bool());
    EXPECT_EQ(read->fingerprint, fingerprint());
    EXPECT_EQ(read->fingerprint.rollbackArchivePath, "/home/ada/.transmit/undo-1.txa");
    ASSERT_EQ(read->placements.size(), 3U);
    EXPECT_EQ(read->placements[0].source, "{DOCUMENTS}/a");
    EXPECT_EQ(read->placements[0].target, "/home/ada/Documents/a");
    EXPECT_EQ(read->placements[0].outcome, RestoreOutcome::Written);
    EXPECT_EQ(read->placements[1].outcome, RestoreOutcome::Skipped);
    EXPECT_EQ(read->placements[2].outcome, RestoreOutcome::Failed);

    // Only the one that is actually on disk counts as done.
    EXPECT_EQ(read->writtenCount(), 1U);
    EXPECT_FALSE(read->complete);
    EXPECT_FALSE(read->tailDiscarded);
}

// The name a "keep both" collision invented is the reason this journal exists.
// Losing it means the resumed run invents a different one and the user is left
// holding two copies of a single file.
TEST_F(RestoreJournalTest, TheNameTheFirstRunInventedComesBack) {
    auto journal = RestoreJournal::begin(journalPath(), fingerprint());
    ASSERT_TRUE(journal.operator bool());
    ASSERT_TRUE((*journal)->recordPlacement(
        placement("{DOCUMENTS}/notes.txt", "/home/ada/Documents/notes~1.txt")));
    ASSERT_TRUE((*journal)->close());

    auto read = readRestoreJournal(journalPath());
    ASSERT_TRUE(read.operator bool());
    ASSERT_EQ(read->placements.size(), 1U);
    EXPECT_EQ(read->placements[0].target, "/home/ada/Documents/notes~1.txt");
}

TEST_F(RestoreJournalTest, EveryPossibleTornTailReadsAsTheRecordsBeforeIt) {
    writeJournal(6);

    std::vector<char> whole;
    {
        std::ifstream file(journalPath(), std::ios::binary);
        whole.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    std::size_t seenSoFar = 0;

    // Cut at every byte. However a restore was interrupted, what comes back is
    // some prefix of what was recorded and never a guess at the rest - and it
    // never shrinks as more of the file survives.
    for (std::uint64_t length = 0; length <= whole.size(); ++length) {
        {
            std::ofstream file(journalPath(), std::ios::binary | std::ios::trunc);
            file.write(whole.data(), static_cast<std::streamsize>(length));
        }

        auto read = readRestoreJournal(journalPath());
        if (!read) {
            // Until the session record is whole there is nothing saying which
            // restore this was, so it is refused rather than guessed at. That
            // may only happen before any placement has been seen.
            EXPECT_EQ(seenSoFar, 0U) << "stopped understanding a journal at " << length;
            continue;
        }

        EXPECT_LE(read->placements.size(), 6U) << "cut at " << length;
        EXPECT_GE(read->placements.size(), seenSoFar) << "lost a record at " << length;
        EXPECT_LE(read->validBytes, length) << "cut at " << length;
        seenSoFar = read->placements.size();

        // Whatever survived is a prefix, in order, of what went in.
        for (std::size_t k = 0; k < read->placements.size(); ++k) {
            EXPECT_EQ(read->placements[k].source, "{DOCUMENTS}/file-" + std::to_string(k))
                << "cut at " << length;
        }
    }

    // And a whole file really does hold all six, or the loop above proved
    // nothing by never getting there.
    EXPECT_EQ(seenSoFar, 6U);
}

TEST_F(RestoreJournalTest, ARecordWhoseBytesChangedIsDroppedAlongWithEverythingAfterIt) {
    writeJournal(4);

    // Flip a byte inside the second placement. Its checksum stops matching, and
    // everything behind it goes too: a record after a damaged one cannot be
    // shown to belong to this run rather than an older, longer one.
    const auto size = static_cast<std::streamoff>(std::filesystem::file_size(journalPath()));
    std::fstream file(journalPath(), std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file.is_open());
    file.seekp(size / 2);
    file.put('\x7F');
    file.close();

    auto read = readRestoreJournal(journalPath());
    ASSERT_TRUE(read.operator bool());
    EXPECT_TRUE(read->tailDiscarded);
    EXPECT_LT(read->placements.size(), 4U);
}

TEST_F(RestoreJournalTest, AJournalCarriedOnFromIsTrimmedToWhatWasRead) {
    const std::uint64_t whole = writeJournal(3);
    truncateTo(whole - 3);  // a half-written record at the end

    auto read = readRestoreJournal(journalPath());
    ASSERT_TRUE(read.operator bool());
    EXPECT_TRUE(read->tailDiscarded);
    const std::uint64_t keep = read->validBytes;

    auto journal = RestoreJournal::reopen(journalPath(), keep);
    ASSERT_TRUE(journal.operator bool());
    ASSERT_TRUE(
        (*journal)->recordPlacement(placement("{DOCUMENTS}/after", "/home/ada/Documents/after")));
    ASSERT_TRUE((*journal)->recordComplete());
    ASSERT_TRUE((*journal)->close());

    // The damaged tail must not have survived in front of the new record: if it
    // had, the next read would stop at it and lose everything written after.
    auto again = readRestoreJournal(journalPath());
    ASSERT_TRUE(again.operator bool());
    EXPECT_FALSE(again->tailDiscarded);
    EXPECT_TRUE(again->complete);
    ASSERT_FALSE(again->placements.empty());
    EXPECT_EQ(again->placements.back().source, "{DOCUMENTS}/after");
}

// A resumed run settles some items a second time. Both records are true
// accounts of what happened; only the later is a true account of what is on
// disk now, so believing both would report a file twice and - worse - could
// hand back the target the first run chose after the second run moved it.
TEST_F(RestoreJournalTest, TheLastWordAboutAnItemIsTheOneKept) {
    auto journal = RestoreJournal::begin(journalPath(), fingerprint());
    ASSERT_TRUE(journal.operator bool());
    ASSERT_TRUE((*journal)->recordPlacement(
        placement("{DOCUMENTS}/a", "/home/ada/Documents/a", RestoreOutcome::Failed)));
    ASSERT_TRUE((*journal)->recordPlacement(placement("{DOCUMENTS}/b", "/home/ada/Documents/b")));
    ASSERT_TRUE((*journal)->recordPlacement(
        placement("{DOCUMENTS}/a", "/home/ada/Documents/a~1", RestoreOutcome::Written)));
    ASSERT_TRUE((*journal)->close());

    auto read = readRestoreJournal(journalPath());
    ASSERT_TRUE(read.operator bool());
    ASSERT_EQ(read->placements.size(), 2U);

    const auto found =
        std::find_if(read->placements.begin(), read->placements.end(),
                     [](const RestorePlacement& p) { return p.source == "{DOCUMENTS}/a"; });
    ASSERT_NE(found, read->placements.end());
    EXPECT_EQ(found->target, "/home/ada/Documents/a~1");
    EXPECT_EQ(found->outcome, RestoreOutcome::Written);
    EXPECT_EQ(read->writtenCount(), 2U);
}

TEST_F(RestoreJournalTest, AFinishedRestoreSaysSo) {
    auto journal = RestoreJournal::begin(journalPath(), fingerprint());
    ASSERT_TRUE(journal.operator bool());
    ASSERT_TRUE((*journal)->recordPlacement(placement("{DOCUMENTS}/a", "/home/ada/Documents/a")));
    ASSERT_TRUE((*journal)->recordComplete());
    ASSERT_TRUE((*journal)->close());

    auto read = readRestoreJournal(journalPath());
    ASSERT_TRUE(read.operator bool());
    EXPECT_TRUE(read->complete);
}

TEST_F(RestoreJournalTest, NoJournalIsNotAnError) {
    auto read = readRestoreJournal(journalPath());
    ASSERT_FALSE(read.operator bool());
    EXPECT_EQ(read.error().code, ErrorCode::NotFound);
}

// The two journals are the same shape and mean opposite things. A capture
// journal read as a restore journal would report every block it holds as an
// item that is already on disk, and the restore would skip them all.
TEST_F(RestoreJournalTest, ACaptureJournalIsNotARestoreJournal) {
    const std::filesystem::path archive = directory_ / "capture.txa";

    JournalFingerprint capture;
    capture.archiveUuid = uuid();
    capture.destination = archive.string();
    auto written = TransferJournal::begin(archive, capture);
    ASSERT_TRUE(written.operator bool());
    ASSERT_TRUE((*written)->close());

    auto read = readRestoreJournal(TransferJournal::pathFor(archive));
    ASSERT_FALSE(read.operator bool());
    EXPECT_EQ(read.error().code, ErrorCode::CorruptArchive);

    // And the other way round, for the same reason.
    auto journal = RestoreJournal::begin(journalPath(), fingerprint());
    ASSERT_TRUE(journal.operator bool());
    ASSERT_TRUE((*journal)->close());
    EXPECT_FALSE(readTransferJournal(journalPath()).operator bool());
}

// Two archives restored into one folder must not read each other's records.
TEST_F(RestoreJournalTest, EachArchiveGetsItsOwnJournal) {
    ArchiveUuid other{};
    other.fill(Byte{0x7E});

    EXPECT_NE(RestoreJournal::pathFor(directory_, uuid()),
              RestoreJournal::pathFor(directory_, other));

    // And the name is derived from the uuid rather than chosen, so the same
    // archive always finds the journal it left behind.
    EXPECT_EQ(RestoreJournal::pathFor(directory_, uuid()),
              RestoreJournal::pathFor(directory_, uuid()));
}

TEST_F(RestoreJournalTest, ARecordThatSaysNothingAboutAnItemIsRefused) {
    // A placement with no source cannot be matched to anything a resumed run
    // is holding. Reporting it as a settled item would mean the run believes
    // it did something it cannot name.
    auto journal = RestoreJournal::begin(journalPath(), fingerprint());
    ASSERT_TRUE(journal.operator bool());
    ASSERT_TRUE((*journal)->recordPlacement(placement("", "/home/ada/Documents/a")));
    ASSERT_TRUE((*journal)->close());

    auto read = readRestoreJournal(journalPath());
    ASSERT_FALSE(read.operator bool());
    EXPECT_EQ(read.error().code, ErrorCode::CorruptArchive);
}

TEST_F(RestoreJournalTest, AJournalWithNoStartIsRefused) {
    writeJournal(2);

    // Cut off the session record at the front. What is left is a list of
    // targets with nothing saying which restore, which archive, or which
    // machine they belong to - so it cannot be acted on.
    const auto size = std::filesystem::file_size(journalPath());
    std::vector<char> bytes(size);
    {
        std::ifstream in(journalPath(), std::ios::binary);
        in.read(bytes.data(), static_cast<std::streamsize>(size));
    }
    // Find where the first record ends: header, then length, then payload.
    const auto* header = reinterpret_cast<const unsigned char*>(bytes.data() + kJournalHeaderSize);
    const std::uint32_t firstLength = static_cast<std::uint32_t>(header[0]) |
                                      (static_cast<std::uint32_t>(header[1]) << 8) |
                                      (static_cast<std::uint32_t>(header[2]) << 16) |
                                      (static_cast<std::uint32_t>(header[3]) << 24);
    const std::size_t after = kJournalHeaderSize + kJournalRecordHeaderSize + firstLength;
    ASSERT_LT(after, size);

    {
        std::ofstream out(journalPath(), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), kJournalHeaderSize);
        out.write(bytes.data() + after, static_cast<std::streamsize>(size - after));
    }

    auto read = readRestoreJournal(journalPath());
    ASSERT_FALSE(read.operator bool());
    EXPECT_EQ(read.error().code, ErrorCode::CorruptArchive);
}

}  // namespace
}  // namespace transmit::format
