// Carrying on with an archive a previous run left unfinished.
//
// This is the one operation in Transmit that writes into a file somebody
// already has data in, so the question every test here asks is the same: is
// what comes out afterwards indistinguishable from what an uninterrupted
// capture would have written? Not "does it open" - a resumed archive that
// opens and has one file's bytes shifted by a block header is worse than one
// that does not open at all, because nothing will ever tell the user.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "format/BlockPacker.h"
#include "format/Container.h"
#include "format/TransferJournal.h"

#include "support/TempDirectory.h"

namespace transmit::format {
namespace {

using NamedFile = std::pair<std::string, std::string>;

class ResumeTest : public testing::Test {
protected:
    void SetUp() override { directory_ = test_support::makeTemporaryDirectory("transmit-resume"); }
    void TearDown() override { test_support::removeTemporaryDirectory(directory_); }

    [[nodiscard]] std::filesystem::path archivePath() const { return directory_ / "capture.txa"; }

    /// Bytes that do not compress, so a part size means what it says. A run
    /// of one repeated character shrinks to nothing and a "split" archive
    /// comes out as a single part.
    static std::string noise(std::uint32_t seed, std::size_t length) {
        std::string out;
        out.reserve(length);
        std::uint32_t state = seed * 2654435761u + 1u;
        for (std::size_t i = 0; i < length; ++i) {
            state = state * 1664525u + 1013904223u;
            out.push_back(static_cast<char>((state >> 24) & 0xFFu));
        }
        return out;
    }

    static ByteBuffer bytesOf(const std::string& text) {
        const auto view = asBytes(text);
        return ByteBuffer(view.begin(), view.end());
    }

    static ArchiveOptions options(std::uint64_t partSize = 0) {
        ArchiveOptions written;
        written.preset = CompressionPreset::Fast;
        // Small, so a handful of short files still fill several blocks and the
        // resume point lands in the middle of the set rather than at its end.
        written.solidBlockSize = 512;
        written.partSize = partSize;
        return written;
    }

    static JournalFingerprint fingerprint(const ArchiveUuid& uuid) {
        JournalFingerprint print;
        print.archiveUuid = uuid;
        print.destination = "capture.txa";
        print.hostName = "workshop";
        print.userName = "ada";
        return print;
    }

    static ManifestEntry entryFor(std::uint64_t id, const std::string& name,
                                  const ByteBuffer& content) {
        ManifestEntry entry;
        entry.id = id;
        entry.domain = DomainId::UserData;
        entry.type = EntryType::File;
        entry.path = TokenizedPath{PathTokenId::Documents, name};
        entry.size = content.size();
        entry.contentHash = Blake2b::hash256(content);
        return entry;
    }

    /// Writes `files` into a new archive and stops without finishing it, the
    /// way a capture stops when the machine does. The journal beside it is
    /// left exactly as the interrupted run would have left it.
    void writePartially(const std::vector<NamedFile>& files, std::uint64_t partSize = 0) {
        auto writerResult = ArchiveWriter::create(archivePath(), options(partSize));
        ASSERT_TRUE(writerResult) << (writerResult ? "" : writerResult.error().toString());
        auto writer = std::move(writerResult).value();

        auto journalResult = TransferJournal::begin(archivePath(), fingerprint(writer->uuid()));
        ASSERT_TRUE(journalResult) << (journalResult ? "" : journalResult.error().toString());
        auto journal = std::move(journalResult).value();

        pack(*writer, *journal, files, 1);

        // No finish(), no footer, no patched part headers. Just gone.
        ASSERT_TRUE(journal->close());
    }

    /// Packs files into the writer, journalling each block as it lands and
    /// each entry once its location is final. This is the order the capture
    /// pipeline keeps, written out by hand so the test does not depend on it.
    void pack(ArchiveWriter& writer, TransferJournal& journal, const std::vector<NamedFile>& files,
              std::uint64_t firstId, Manifest* manifest = nullptr) {
        std::vector<ManifestEntry> entries;
        std::vector<BlockPacker::PlacementId> handles;

        std::size_t blocksRecorded = writer.blocks().size();
        const auto recordNewBlocks = [&writer, &journal, &blocksRecorded]() {
            while (blocksRecorded < writer.blocks().size()) {
                const BlockRecord& record = writer.blocks()[blocksRecorded];
                JournalBlock written;
                written.blockId = record.blockId;
                written.streamOffset = record.streamOffset;
                written.rawSize = record.rawSize;
                written.storedSize = record.storedSize;
                written.codec = record.codec;
                written.encrypted = record.encrypted;
                // Every block but the last is followed by another, so the
                // stream length after this one is the next one's offset. For
                // the last, it is where the writer has got to.
                written.logicalEnd = blocksRecorded + 1 < writer.blocks().size()
                                         ? writer.blocks()[blocksRecorded + 1].streamOffset
                                         : writer.logicalLength();
                ASSERT_TRUE(journal.recordBlock(written));
                ++blocksRecorded;
            }
        };

        BlockPacker packer(options().solidBlockSize,
                           [&writer](ByteView raw) -> Result<std::uint32_t> {
                               const std::uint32_t blockId = writer.nextBlockId();
                               TRANSMIT_TRY(prepared, writer.prepare(blockId, raw));
                               TRANSMIT_CHECK(writer.writePrepared(prepared));
                               return blockId;
                           });

        std::uint64_t nextId = firstId;
        for (const auto& [name, text] : files) {
            const ByteBuffer content = bytesOf(text);
            entries.push_back(entryFor(nextId++, name, content));

            auto handle = packer.add(entries.back().contentHash, content);
            ASSERT_TRUE(handle) << (handle ? "" : handle.error().toString());
            handles.push_back(*handle);

            recordNewBlocks();
            for (const BlockPacker::PlacementId id : packer.takeResolved()) {
                const auto position = std::find(handles.begin(), handles.end(), id);
                ASSERT_NE(position, handles.end());
                const auto index = static_cast<std::size_t>(position - handles.begin());
                const auto located = packer.location(id);
                ASSERT_TRUE(located) << (located ? "" : located.error().toString());
                entries[index].location = *located;
                ASSERT_TRUE(journal.recordEntry(entries[index]));
            }
        }

        ASSERT_TRUE(packer.flush());
        recordNewBlocks();
        for (const BlockPacker::PlacementId id : packer.takeResolved()) {
            const auto position = std::find(handles.begin(), handles.end(), id);
            ASSERT_NE(position, handles.end());
            const auto index = static_cast<std::size_t>(position - handles.begin());
            const auto located = packer.location(id);
            ASSERT_TRUE(located) << (located ? "" : located.error().toString());
            entries[index].location = *located;
            ASSERT_TRUE(journal.recordEntry(entries[index]));
        }
        ASSERT_TRUE(journal.sync());

        if (manifest != nullptr) {
            for (ManifestEntry& entry : entries) {
                manifest->entries.push_back(std::move(entry));
            }
        }
    }

    /// Reads every file out of a finished archive.
    [[nodiscard]] std::vector<NamedFile> readBack(const std::filesystem::path& anyPart) {
        std::vector<NamedFile> found;
        auto readerResult = ArchiveReader::open(anyPart);
        EXPECT_TRUE(readerResult) << (readerResult ? "" : readerResult.error().toString());
        if (!readerResult) {
            return found;
        }
        auto reader = std::move(readerResult).value();

        auto manifest = reader->manifest();
        EXPECT_TRUE(manifest) << (manifest ? "" : manifest.error().toString());
        if (!manifest) {
            return found;
        }

        for (const ManifestEntry& entry : (*manifest)->entries) {
            auto content = reader->readEntry(entry);
            EXPECT_TRUE(content) << entry.path.relative << ": "
                                 << (content ? "" : content.error().toString());
            if (!content) {
                continue;
            }
            found.emplace_back(entry.path.relative, std::string(asText(*content)));
        }
        return found;
    }

    std::filesystem::path directory_;
};

const std::vector<NamedFile> kFirstHalf = {
    {"notes.txt", std::string(300, 'a')},
    {"budget.csv", std::string(300, 'b')},
    {"letter.odt", std::string(300, 'c')},
};

const std::vector<NamedFile> kSecondHalf = {
    {"photo.jpg", std::string(300, 'd')},
    {"README.md", std::string(300, 'e')},
};

TEST_F(ResumeTest, AnInterruptedArchiveCannotBeOpened) {
    writePartially(kFirstHalf);

    // The premise of the whole feature: what an interrupted run leaves cannot
    // be read, so nothing but the journal knows what is in it.
    auto reader = ArchiveReader::open(archivePath());
    if (reader) {
        auto manifest = (*reader)->manifest();
        EXPECT_FALSE(manifest) << "an unfinished archive gave up a manifest";
    }
    EXPECT_TRUE(std::filesystem::exists(TransferJournal::pathFor(archivePath())));
}

TEST_F(ResumeTest, CarryingOnProducesAnArchiveHoldingBothHalves) {
    writePartially(kFirstHalf);

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents) << contents.error().toString();
    ASSERT_FALSE(contents->complete);
    ASSERT_EQ(contents->entries.size(), kFirstHalf.size());

    ArchiveWriter::ResumePoint point;
    point.logicalLength = contents->resumableLength();
    for (const JournalBlock& block : contents->blocks) {
        point.blocks.push_back(block.asBlockRecord());
    }

    auto writerResult = ArchiveWriter::resume(archivePath(), options(), point);
    ASSERT_TRUE(writerResult) << (writerResult ? "" : writerResult.error().toString());
    auto writer = std::move(writerResult).value();

    auto journalResult = TransferJournal::reopen(archivePath(), contents->validBytes);
    ASSERT_TRUE(journalResult) << (journalResult ? "" : journalResult.error().toString());
    auto journal = std::move(journalResult).value();

    Manifest manifest;
    manifest.source.os = OsFamily::Linux;
    manifest.source.homeDirectory = "/home/ada";
    for (const ManifestEntry& entry : contents->entries) {
        manifest.entries.push_back(entry);
    }

    pack(*writer, *journal, kSecondHalf, contents->entries.size() + 1, &manifest);

    ASSERT_TRUE(writer->finish(manifest));
    ASSERT_TRUE(journal->recordComplete());
    ASSERT_TRUE(journal->close());
    ASSERT_TRUE(TransferJournal::discard(archivePath()));

    const auto found = readBack(archivePath());
    ASSERT_EQ(found.size(), kFirstHalf.size() + kSecondHalf.size());

    std::vector<NamedFile> expected = kFirstHalf;
    expected.insert(expected.end(), kSecondHalf.begin(), kSecondHalf.end());
    for (const auto& [name, text] : expected) {
        const auto position = std::find_if(found.begin(), found.end(),
                                           [&name](const NamedFile& f) { return f.first == name; });
        ASSERT_NE(position, found.end()) << name << " is missing from the resumed archive";
        EXPECT_EQ(position->second, text) << name << " came back with the wrong bytes";
    }
}

// The bytes after the last block the journal saw are of unknown provenance: a
// half-written block, or a whole one that never got recorded. Appending after
// them would put a hole in the middle of the archive that every later offset
// is measured against.
TEST_F(ResumeTest, TheBytesPastTheResumePointAreCutOff) {
    writePartially(kFirstHalf);

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents);
    const std::uint64_t resumeAt = contents->resumableLength();

    // A torn tail: the start of a block that never finished being written.
    {
        std::ofstream file(archivePath(), std::ios::binary | std::ios::app);
        const std::string rubbish(777, '\xAB');
        file.write(rubbish.data(), static_cast<std::streamsize>(rubbish.size()));
    }
    const auto grown = std::filesystem::file_size(archivePath());

    ArchiveWriter::ResumePoint point;
    point.logicalLength = resumeAt;
    for (const JournalBlock& block : contents->blocks) {
        point.blocks.push_back(block.asBlockRecord());
    }

    auto writerResult = ArchiveWriter::resume(archivePath(), options(), point);
    ASSERT_TRUE(writerResult) << (writerResult ? "" : writerResult.error().toString());
    auto writer = std::move(writerResult).value();

    EXPECT_LT(std::filesystem::file_size(archivePath()), grown) << "the torn tail was kept";
    EXPECT_EQ(writer->logicalLength(), resumeAt);

    auto journal = TransferJournal::reopen(archivePath(), contents->validBytes);
    ASSERT_TRUE(journal);

    Manifest manifest;
    manifest.source.os = OsFamily::Linux;
    for (const ManifestEntry& entry : contents->entries) {
        manifest.entries.push_back(entry);
    }
    pack(*writer, **journal, kSecondHalf, contents->entries.size() + 1, &manifest);
    ASSERT_TRUE(writer->finish(manifest));

    const auto found = readBack(archivePath());
    EXPECT_EQ(found.size(), kFirstHalf.size() + kSecondHalf.size());
}

TEST_F(ResumeTest, ResumingPastWhatTheDriveHoldsIsRefused) {
    writePartially(kFirstHalf);

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents);

    ArchiveWriter::ResumePoint point;
    point.logicalLength = contents->resumableLength() + 4096;
    for (const JournalBlock& block : contents->blocks) {
        point.blocks.push_back(block.asBlockRecord());
    }

    const auto writer = ArchiveWriter::resume(archivePath(), options(), point);
    EXPECT_FALSE(writer) << "carried on past the end of what was written";
}

TEST_F(ResumeTest, AnArchiveThatWasAlreadyFinishedIsNotResumed) {
    {
        auto writerResult = ArchiveWriter::create(archivePath(), options());
        ASSERT_TRUE(writerResult);
        auto writer = std::move(writerResult).value();
        Manifest manifest;
        manifest.source.os = OsFamily::Linux;
        ASSERT_TRUE(writer->finish(manifest));
    }

    ArchiveWriter::ResumePoint point;
    point.logicalLength = ArchiveHeader::kSize;
    const auto writer = ArchiveWriter::resume(archivePath(), options(), point);
    EXPECT_FALSE(writer) << "a finished archive was reopened for writing";
}

TEST_F(ResumeTest, APartFromADifferentCaptureIsRefused) {
    writePartially(kFirstHalf);

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents);

    ArchiveWriter::ResumePoint point;
    point.logicalLength = contents->resumableLength();
    for (const JournalBlock& block : contents->blocks) {
        point.blocks.push_back(block.asBlockRecord());
    }

    // The same name, a different capture: the file was replaced between the
    // interrupted run and this one.
    {
        auto other = ArchiveWriter::create(directory_ / "other.txa", options());
        ASSERT_TRUE(other);
        Manifest manifest;
        manifest.source.os = OsFamily::Linux;
        ASSERT_TRUE((*other)->finish(manifest));
    }
    std::filesystem::remove(archivePath());
    std::filesystem::copy_file(directory_ / "other.txa", archivePath());

    const auto writer = ArchiveWriter::resume(archivePath(), options(), point);
    EXPECT_FALSE(writer) << "carried on into an archive from a different capture";
}

TEST_F(ResumeTest, ASplitArchiveCarriesOnInTheRightPart) {
    // Small enough, against content that does not compress, that the first
    // half needs more than one part.
    constexpr std::uint64_t kPartSize = 1024;
    const std::vector<NamedFile> firstHalf = {
        {"one.bin", noise(1, 600)},
        {"two.bin", noise(2, 600)},
        {"three.bin", noise(3, 600)},
    };
    const std::vector<NamedFile> secondHalf = {
        {"four.bin", noise(4, 600)},
        {"five.bin", noise(5, 600)},
    };
    writePartially(firstHalf, kPartSize);

    const auto firstPart = partPathFor(archivePath(), 1);
    ASSERT_TRUE(std::filesystem::exists(firstPart));
    ASSERT_TRUE(std::filesystem::exists(partPathFor(archivePath(), 2)))
        << "the fixture did not produce a split set";

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents);

    ArchiveWriter::ResumePoint point;
    point.logicalLength = contents->resumableLength();
    for (const JournalBlock& block : contents->blocks) {
        point.blocks.push_back(block.asBlockRecord());
    }

    auto writerResult = ArchiveWriter::resume(archivePath(), options(kPartSize), point);
    ASSERT_TRUE(writerResult) << (writerResult ? "" : writerResult.error().toString());
    auto writer = std::move(writerResult).value();

    auto journal = TransferJournal::reopen(archivePath(), contents->validBytes);
    ASSERT_TRUE(journal);

    Manifest manifest;
    manifest.source.os = OsFamily::Linux;
    for (const ManifestEntry& entry : contents->entries) {
        manifest.entries.push_back(entry);
    }
    pack(*writer, **journal, secondHalf, contents->entries.size() + 1, &manifest);
    ASSERT_TRUE(writer->finish(manifest));

    const auto found = readBack(firstPart);
    ASSERT_EQ(found.size(), firstHalf.size() + secondHalf.size());

    std::vector<NamedFile> expected = firstHalf;
    expected.insert(expected.end(), secondHalf.begin(), secondHalf.end());
    for (const auto& [name, text] : expected) {
        const auto position = std::find_if(found.begin(), found.end(),
                                           [&name](const NamedFile& f) { return f.first == name; });
        ASSERT_NE(position, found.end()) << name << " is missing from the resumed set";
        EXPECT_EQ(position->second, text) << name << " came back with the wrong bytes";
    }
}

// A resumed capture meets the same file again - the run stopped after it was
// stored but before the whole capture finished, so it comes round a second
// time. Without the deduplication table the previous run had in memory, the
// bytes go in twice.
TEST_F(ResumeTest, ContentThePreviousRunAlreadyStoredIsNotStoredAgain) {
    writePartially(kFirstHalf);

    const auto contents = readTransferJournal(archivePath());
    ASSERT_TRUE(contents);
    ASSERT_FALSE(contents->entries.empty());

    ArchiveWriter::ResumePoint point;
    point.logicalLength = contents->resumableLength();
    for (const JournalBlock& block : contents->blocks) {
        point.blocks.push_back(block.asBlockRecord());
    }

    auto writerResult = ArchiveWriter::resume(archivePath(), options(), point);
    ASSERT_TRUE(writerResult);
    auto writer = std::move(writerResult).value();

    BlockPacker packer(options().solidBlockSize, [&writer](ByteView raw) -> Result<std::uint32_t> {
        const std::uint32_t blockId = writer->nextBlockId();
        TRANSMIT_TRY(prepared, writer->prepare(blockId, raw));
        TRANSMIT_CHECK(writer->writePrepared(prepared));
        return blockId;
    });

    for (const ManifestEntry& entry : contents->entries) {
        packer.remember(entry.contentHash, entry.location);
    }

    // The very first file again, byte for byte.
    const ByteBuffer repeated = bytesOf(kFirstHalf.front().second);
    auto handle = packer.add(Blake2b::hash256(repeated), repeated);
    ASSERT_TRUE(handle);

    EXPECT_TRUE(packer.isDeduplicated(*handle))
        << "a file the previous run stored was stored a second time";
    EXPECT_EQ(packer.deduplicatedBytes(), repeated.size());

    const auto located = packer.location(*handle);
    ASSERT_TRUE(located);
    EXPECT_EQ(*located, contents->entries.front().location);
}

}  // namespace
}  // namespace transmit::format
