// What "verified" is allowed to mean.
//
// Reading every block back and finding it decompresses proves the archive is
// not shredded. It does not prove the entry table still points at the right
// bytes, that the footer belongs to the manifest it names, or that the file
// somebody is about to restore is the file that was captured. Each of those
// was a gap; these are the tests that keep them shut.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "format/BlockPacker.h"
#include "format/ChecksumSidecar.h"
#include "format/Container.h"
#include "format/FileIo.h"
#include "format/hash/ContentHash.h"
#include "format/hash/Crc32.h"

#include "support/TempDirectory.h"

namespace transmit::format {
namespace {

class VerificationTest : public testing::Test {
protected:
    void SetUp() override { directory_ = test_support::makeTemporaryDirectory("transmit-verify"); }

    void TearDown() override { test_support::removeTemporaryDirectory(directory_); }

    [[nodiscard]] std::filesystem::path archivePath(const std::string& name = "test.txa") const {
        return directory_ / name;
    }

    static ByteBuffer bytesOf(std::string_view text) {
        const auto view = asBytes(text);
        return ByteBuffer(view.begin(), view.end());
    }

    /// Writes an archive, giving the caller a chance to spoil the manifest
    /// between building it and committing it. That is the only way to produce
    /// an archive whose blocks are all sound and whose table is not - which is
    /// exactly the case the entry checks exist for, and which no amount of
    /// flipping bits in the file will produce.
    Manifest writeArchive(const std::filesystem::path& path,
                          const std::vector<std::pair<std::string, ByteBuffer>>& files,
                          const std::function<void(Manifest&)>& spoil = {}) {
        ArchiveOptions options;
        options.preset = CompressionPreset::Fast;
        options.solidBlockSize = 4096;

        auto writerResult = ArchiveWriter::create(path, options);
        EXPECT_TRUE(writerResult) << (writerResult ? "" : writerResult.error().toString());
        if (!writerResult) {
            return {};
        }
        auto writer = std::move(writerResult).value();

        Manifest manifest;
        manifest.source.os = OsFamily::Linux;
        manifest.source.homeDirectory = "/home/bob";

        BlockPacker packer(4096, [&writer](ByteView raw) -> Result<std::uint32_t> {
            const std::uint32_t blockId = writer->nextBlockId();
            TRANSMIT_TRY(prepared, writer->prepare(blockId, raw));
            TRANSMIT_CHECK(writer->writePrepared(prepared));
            return blockId;
        });

        std::vector<BlockPacker::PlacementId> handles;
        std::uint64_t nextId = 1;
        for (const auto& [name, content] : files) {
            ManifestEntry entry;
            entry.id = nextId++;
            entry.domain = DomainId::UserData;
            entry.type = EntryType::File;
            entry.path = TokenizedPath{PathTokenId::Documents, name};
            entry.size = content.size();

            const ContentDigests digests = hashContent(content);
            entry.contentHash = digests.blake2b;
            entry.contentMd5 = digests.md5;

            auto handle = packer.add(entry.contentHash, content);
            EXPECT_TRUE(handle);
            handles.push_back(handle ? *handle : 0);
            manifest.entries.push_back(entry);
        }

        EXPECT_TRUE(packer.flush());
        for (std::size_t i = 0; i < handles.size(); ++i) {
            const auto location = packer.location(handles[i]);
            EXPECT_TRUE(location);
            if (location) {
                manifest.entries[i].location = *location;
            }
        }

        if (spoil) {
            spoil(manifest);
        }
        EXPECT_TRUE(writer->finish(manifest));
        return manifest;
    }

    std::filesystem::path directory_;
};

// ------------------------------------------------------------ the sidecar

TEST_F(VerificationTest, TheSidecarNamesThePartsAndTheirContents) {
    const auto path = archivePath();
    const Manifest manifest = writeArchive(path, {{"notes.txt", bytesOf("a few lines of notes")},
                                                  {"more.txt", bytesOf("and some more")}});

    SidecarOptions options;
    options.archiveName = "test.txa";
    const auto sidecar =
        writeChecksumSidecar(directory_ / "test.txa.md5", {path}, manifest, options);
    ASSERT_TRUE(sidecar) << (sidecar ? "" : sidecar.error().toString());

    std::ifstream file(*sidecar);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

    // The part line is a real md5sum line, and it is the true hash of the file
    // on disk - which is the whole claim the sidecar makes.
    const auto expected = md5OfFile(path);
    ASSERT_TRUE(expected);
    EXPECT_NE(text.find(toHex(*expected) + "  test.txa"), std::string::npos) << text;

    // And the contents are there to be read, as comments.
    EXPECT_NE(text.find("# entry "), std::string::npos) << text;
    EXPECT_NE(text.find("notes.txt"), std::string::npos) << text;
    for (const ManifestEntry& entry : manifest.entries) {
        EXPECT_NE(text.find(toHex(entry.contentMd5)), std::string::npos)
            << entry.path.toDisplayString();
    }
}

TEST_F(VerificationTest, TheSidecarCanBeReadBack) {
    const auto path = archivePath();
    const Manifest manifest = writeArchive(path, {{"notes.txt", bytesOf("hello")}});

    const auto written = writeChecksumSidecar(directory_ / "test.txa.md5", {path}, manifest);
    ASSERT_TRUE(written);

    const auto parts = readChecksumSidecar(*written);
    ASSERT_TRUE(parts) << (parts ? "" : parts.error().toString());
    ASSERT_EQ(parts->size(), 1U);
    EXPECT_EQ(parts->front().fileName, "test.txa");

    const auto expected = md5OfFile(path);
    ASSERT_TRUE(expected);
    EXPECT_EQ(parts->front().md5, *expected);
}

// Encrypting an archive is a request that the names of somebody's files not be
// readable from the drive. A sidecar listing every path beside it would hand
// them over in plain text, so it does not do that unless it is asked to.
TEST_F(VerificationTest, TheSidecarLeavesTheNamesOutWhenAsked) {
    const auto path = archivePath();
    const Manifest manifest = writeArchive(path, {{"private-diary.txt", bytesOf("hello")}});

    SidecarOptions options;
    options.includeEntries = false;
    const auto written = writeChecksumSidecar(directory_ / "quiet.md5", {path}, manifest, options);
    ASSERT_TRUE(written);

    std::ifstream file(*written);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    EXPECT_EQ(text.find("private-diary"), std::string::npos) << text;
    EXPECT_EQ(text.find("# entry "), std::string::npos) << text;

    // The part is still listed: that is the line md5sum acts on.
    const auto parts = readChecksumSidecar(*written);
    ASSERT_TRUE(parts);
    EXPECT_EQ(parts->size(), 1U);
}

// Somebody may well add a note of their own to the file. Refusing to read it
// because of that would be the least useful possible response.
TEST_F(VerificationTest, ReadingSkipsCommentsAndAnythingElseUnexpected) {
    const auto path = directory_ / "hand-written.md5";
    {
        // Binary, so the line endings under test are the ones written here
        // rather than whatever the platform's text mode decides.
        std::ofstream file(path, std::ios::binary);
        file << "# a comment\n";
        file << "\n";
        file << "this line is not a checksum at all\n";
        file << "00112233445566778899aabbccddeeff  first.txa\n";
        file << "  indented rubbish\n";
        file << "ffeeddccbbaa99887766554433221100 *second.txa\n";
    }

    const auto parts = readChecksumSidecar(path);
    ASSERT_TRUE(parts) << (parts ? "" : parts.error().toString());
    ASSERT_EQ(parts->size(), 2U);
    EXPECT_EQ(parts->at(0).fileName, "first.txa");
    EXPECT_EQ(parts->at(1).fileName, "second.txa");
    EXPECT_EQ(toHex(parts->at(0).md5), "00112233445566778899aabbccddeeff");
}

// This file crosses operating systems for a living - it is written beside the
// archive on a USB stick precisely so the archive can be checked on the machine
// it is going to. So a sidecar with CRLF endings is the ordinary case, not an
// exotic one, and a carriage return left on the end of a name makes the part it
// names match nothing. That failure is the bad kind: the archive is perfectly
// sound and the check says otherwise.
TEST_F(VerificationTest, ReadingHandlesWindowsLineEndings) {
    const auto path = directory_ / "from-windows.md5";
    {
        std::ofstream file(path, std::ios::binary);
        file << "# written on a machine that ends its lines with CRLF\r\n";
        file << "00112233445566778899aabbccddeeff  first.txa\r\n";
        file << "ffeeddccbbaa99887766554433221100 *second.txa\r\n";
    }

    const auto parts = readChecksumSidecar(path);
    ASSERT_TRUE(parts) << (parts ? "" : parts.error().toString());
    ASSERT_EQ(parts->size(), 2U);
    EXPECT_EQ(parts->at(0).fileName, "first.txa");
    EXPECT_EQ(parts->at(1).fileName, "second.txa");
    EXPECT_EQ(toHex(parts->at(0).md5), "00112233445566778899aabbccddeeff");
}

// A file with no newline after its last line is what a text editor that does
// not add one leaves behind, and the last part is the one most likely to
// matter.
TEST_F(VerificationTest, ReadingHandlesAFinalLineWithNoEndingAtAll) {
    const auto path = directory_ / "unterminated.md5";
    {
        std::ofstream file(path, std::ios::binary);
        file << "00112233445566778899aabbccddeeff  only.txa";
    }

    const auto parts = readChecksumSidecar(path);
    ASSERT_TRUE(parts) << (parts ? "" : parts.error().toString());
    ASSERT_EQ(parts->size(), 1U);
    EXPECT_EQ(parts->at(0).fileName, "only.txa");
}

// ---------------------------------------------------- the entry-level checks

TEST_F(VerificationTest, VerifyingChecksEveryFileNotJustEveryBlock) {
    const auto path = archivePath();
    writeArchive(path, {{"notes.txt", bytesOf("a few lines of notes")}});

    auto reader = ArchiveReader::open(path);
    ASSERT_TRUE(reader) << (reader ? "" : reader.error().toString());
    EXPECT_TRUE((*reader)->verifyAllBlocks(nullptr));
}

// The blocks are all sound here. Only the recorded hash is wrong - which is
// what a damaged manifest looks like, and what block-level verification used
// to walk straight past.
TEST_F(VerificationTest, AnEntryWhoseHashIsWrongIsRefused) {
    const auto path = archivePath();
    writeArchive(path, {{"notes.txt", bytesOf("a few lines of notes")}},
                 [](Manifest& manifest) { manifest.entries.front().contentHash[0] ^= Byte{0x01}; });

    auto reader = ArchiveReader::open(path);
    ASSERT_TRUE(reader);
    const Status verified = (*reader)->verifyAllBlocks(nullptr);
    ASSERT_FALSE(verified);
    EXPECT_EQ(verified.error().code, ErrorCode::IntegrityMismatch);
}

TEST_F(VerificationTest, AnEntryWhoseMd5IsWrongIsRefused) {
    const auto path = archivePath();
    writeArchive(path, {{"notes.txt", bytesOf("a few lines of notes")}},
                 [](Manifest& manifest) { manifest.entries.front().contentMd5[0] ^= Byte{0x01}; });

    auto reader = ArchiveReader::open(path);
    ASSERT_TRUE(reader);
    const Status verified = (*reader)->verifyAllBlocks(nullptr);
    ASSERT_FALSE(verified);
    EXPECT_EQ(verified.error().code, ErrorCode::IntegrityMismatch);
}

// An MD5 of zero means "none was recorded", not "this file hashes to zero", so
// an archive written without them still verifies.
TEST_F(VerificationTest, AnArchiveWithoutMd5StillVerifies) {
    const auto path = archivePath();
    writeArchive(path, {{"notes.txt", bytesOf("a few lines of notes")}},
                 [](Manifest& manifest) { manifest.entries.front().contentMd5 = Digest128{}; });

    auto reader = ArchiveReader::open(path);
    ASSERT_TRUE(reader);
    EXPECT_TRUE((*reader)->verifyAllBlocks(nullptr));
}

// Caught when the manifest is loaded rather than when the entry is read, so an
// archive whose table is wrong says so at the start of a restore instead of
// half way through one that has already written a thousand files.
TEST_F(VerificationTest, AnEntryPointingOutsideItsBlockIsRefusedOnOpening) {
    const auto path = archivePath();
    writeArchive(path, {{"notes.txt", bytesOf("a few lines of notes")}},
                 [](Manifest& manifest) { manifest.entries.front().location.offset += 1u << 20; });

    auto reader = ArchiveReader::open(path);
    ASSERT_TRUE(reader) << (reader ? "" : reader.error().toString());
    const auto loaded = (*reader)->manifest();
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error().code, ErrorCode::CorruptArchive);
}

TEST_F(VerificationTest, AnEntryNamingABlockThatIsNotThereIsRefused) {
    const auto path = archivePath();
    writeArchive(path, {{"notes.txt", bytesOf("a few lines of notes")}},
                 [](Manifest& manifest) { manifest.entries.front().location.blockId = 4242; });

    auto reader = ArchiveReader::open(path);
    ASSERT_TRUE(reader);
    const auto loaded = (*reader)->manifest();
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error().code, ErrorCode::CorruptArchive);
}

// ------------------------------------------------------------- the footer

// The footer is written last and is the only thing that says an archive is
// finished, so it gets to have an opinion about which manifest belongs to it.
// The hash has been in the format from the start and was never once compared.
TEST_F(VerificationTest, AFooterThatNamesADifferentManifestIsRefused) {
    const auto path = archivePath();
    writeArchive(path, {{"notes.txt", bytesOf("a few lines of notes")}});

    // Rewrite the footer's manifest hash, and its checksum with it - otherwise
    // the footer's own CRC catches this first and the test proves nothing
    // about the hash.
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file);
        file.seekg(0, std::ios::end);
        const auto size = static_cast<std::streamoff>(file.tellg());

        std::array<char, ArchiveFooter::kSize> footer{};
        file.seekg(size - static_cast<std::streamoff>(footer.size()));
        file.read(footer.data(), static_cast<std::streamsize>(footer.size()));

        footer[36] = static_cast<char>(footer[36] ^ 0x01);
        const std::uint32_t checksum =
            crc32(ByteView(reinterpret_cast<const Byte*>(footer.data()), 44));
        for (std::size_t i = 0; i < 4; ++i) {
            footer[44 + i] = static_cast<char>((checksum >> (i * 8)) & 0xffU);
        }

        file.seekp(size - static_cast<std::streamoff>(footer.size()));
        file.write(footer.data(), static_cast<std::streamsize>(footer.size()));
    }

    auto reader = ArchiveReader::open(path);
    ASSERT_TRUE(reader) << (reader ? "" : reader.error().toString());
    const auto loaded = (*reader)->manifest();
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error().code, ErrorCode::IntegrityMismatch);
}

// --------------------------------------------------------- the block cache

// The cache used to be first-in-first-out wearing an LRU's name: a hit did not
// move the block, so a block every entry needs was thrown away as soon as
// three others had been read. A restore that alternates between two blocks
// then decompresses sixty-four megabytes of zstd for every single file.
TEST_F(VerificationTest, ABlockThatKeepsBeingUsedIsNotEvicted) {
    const auto path = archivePath();

    // Four files, each large enough to need a block of its own at this solid
    // block size, so the ids are known and the cache has to make choices.
    std::vector<std::pair<std::string, ByteBuffer>> files;
    for (int i = 0; i < 4; ++i) {
        files.emplace_back("file" + std::to_string(i) + ".bin",
                           ByteBuffer(6000, static_cast<Byte>('a' + i)));
    }
    const Manifest manifest = writeArchive(path, files);

    std::vector<std::uint32_t> blocks;
    for (const ManifestEntry& entry : manifest.entries) {
        if (std::find(blocks.begin(), blocks.end(), entry.location.blockId) == blocks.end()) {
            blocks.push_back(entry.location.blockId);
        }
    }
    ASSERT_GE(blocks.size(), 3U) << "this needs more blocks than the cache can hold";

    auto opening = ArchiveReader::open(path);
    ASSERT_TRUE(opening);
    auto reader = std::move(opening).value();
    reader->setBlockCacheLimit(2);

    // One block used every round, and the other slot rotating through the
    // rest. Three distinct blocks against a cache of two is the case that
    // tells the two policies apart: least-recently-used never evicts the hot
    // one, because it was just used; first-in-first-out evicts it precisely
    // because it went in first, and reads it again every round.
    const std::uint32_t hot = blocks.front();
    for (int round = 0; round < 6; ++round) {
        ASSERT_TRUE(reader->readBlock(hot));
        ASSERT_TRUE(reader->readBlock(blocks[1 + static_cast<std::size_t>(round) % 2]));
    }

    EXPECT_GT(reader->cacheHits(), 0U);
    // The hot block once, and the two cold ones each time they come round.
    EXPECT_LE(reader->blocksDecompressed(), 7U)
        << "the block in constant use was thrown away and read again";
}

TEST_F(VerificationTest, TheCacheLimitIsHonoured) {
    const auto path = archivePath();

    std::vector<std::pair<std::string, ByteBuffer>> files;
    for (int i = 0; i < 5; ++i) {
        files.emplace_back("file" + std::to_string(i) + ".bin",
                           ByteBuffer(6000, static_cast<Byte>('a' + i)));
    }
    const Manifest manifest = writeArchive(path, files);

    auto opening = ArchiveReader::open(path);
    ASSERT_TRUE(opening);
    auto reader = std::move(opening).value();
    reader->setBlockCacheLimit(1);

    // Two blocks, alternating, with room for one. Every read is a miss, and
    // nothing is claimed otherwise.
    std::vector<std::uint32_t> ids;
    for (const ManifestEntry& entry : manifest.entries) {
        if (std::find(ids.begin(), ids.end(), entry.location.blockId) == ids.end()) {
            ids.push_back(entry.location.blockId);
        }
    }
    ASSERT_GE(ids.size(), 2U);

    for (int round = 0; round < 4; ++round) {
        ASSERT_TRUE(reader->readBlock(ids[0]));
        ASSERT_TRUE(reader->readBlock(ids[1]));
    }
    EXPECT_EQ(reader->cacheHits(), 0U);
    EXPECT_EQ(reader->blocksDecompressed(), 8U);
}

}  // namespace
}  // namespace transmit::format
