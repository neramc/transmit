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

namespace transmit::format {
namespace {

class VerificationTest : public testing::Test {
protected:
    void SetUp() override {
        directory_ =
            std::filesystem::temp_directory_path() /
            ("transmit-verify-" +
             std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()) + "-" +
             std::to_string(counter_++));
        std::filesystem::remove_all(directory_);
        std::filesystem::create_directories(directory_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(directory_, ec);
    }

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
    static inline int counter_ = 0;
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
        std::ofstream file(path);
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

}  // namespace
}  // namespace transmit::format
