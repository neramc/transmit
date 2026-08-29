/// Writes a random archive and reads it back, over and over.
///
/// The unit tests check the shapes somebody thought of. This checks the
/// one property that must hold for every shape at all: whatever goes into
/// an archive comes out of it byte for byte, with its metadata, however
/// the run was configured.
///
/// It is the test that is supposed to fail when an optimisation is
/// wrong - which is why it exists before the optimisations do.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "format/BlockPacker.h"
#include "format/Container.h"
#include "format/Manifest.h"
#include "property/Generators.h"

namespace transmit::format {
namespace {

using property::Gen;

struct GeneratedFile {
    std::string path;
    ByteBuffer content;
    std::int64_t modifiedUnixNs = 0;
    std::uint32_t mode = 0;
};

/// The awkward sizes, deliberately: nothing, one byte, exactly a block,
/// one byte over a block. The rest are random.
std::vector<GeneratedFile> generateFiles(Gen& gen, std::uint64_t blockSize) {
    std::vector<GeneratedFile> files;
    const int count = gen.inRange(1, 24);
    files.reserve(static_cast<std::size_t>(count));

    std::map<std::string, int> usedPaths;
    for (int i = 0; i < count; ++i) {
        GeneratedFile file;

        std::string path = gen.relativePath();
        // Paths must be distinct: two entries at one path is a different
        // property (the sanitizer's), checked in its own suite.
        if (const auto seen = usedPaths.find(path); seen != usedPaths.end()) {
            path += "-" + std::to_string(++seen->second);
        } else {
            usedPaths.emplace(path, 0);
        }
        file.path = path;

        std::size_t length = 0;
        switch (gen.inRange(0, 6)) {
            case 0: length = 0; break;
            case 1: length = 1; break;
            case 2: length = static_cast<std::size_t>(blockSize); break;
            case 3: length = static_cast<std::size_t>(blockSize) + 1; break;
            default: length = gen.size(0, static_cast<std::size_t>(blockSize) * 2); break;
        }
        file.content = gen.bytes(length);

        // Repeated content on purpose, so deduplication runs.
        if (!files.empty() && gen.chance(25)) {
            file.content = files.at(gen.size(0, files.size() - 1)).content;
        }

        file.modifiedUnixNs = static_cast<std::int64_t>(gen.next() % 2'000'000'000'000'000'000ull);
        file.mode = static_cast<std::uint32_t>(gen.inRange(0400, 0777));
        files.push_back(std::move(file));
    }
    return files;
}

class RoundTripProperty : public testing::TestWithParam<int> {
protected:
    void SetUp() override {
        directory_ = std::filesystem::temp_directory_path() /
                     ("transmit-property-" + std::to_string(GetParam()));
        std::filesystem::remove_all(directory_);
        std::filesystem::create_directories(directory_);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    std::filesystem::path directory_;
};

TEST_P(RoundTripProperty, EveryArchiveGivesBackWhatWentIntoIt) {
    const std::uint64_t seed = property::baseSeed() + static_cast<std::uint64_t>(GetParam());
    Gen gen(seed);
    SCOPED_TRACE("TRANSMIT_PROPERTY_SEED=" + std::to_string(seed));

    ArchiveOptions options;
    options.preset = gen.pick(std::vector<CompressionPreset>{
        CompressionPreset::Fast, CompressionPreset::Balanced, CompressionPreset::Maximum});
    options.solidBlockSize = gen.pick(std::vector<std::uint64_t>{1024, 4096, 65536});
    options.partSize = gen.chance(40)
                           ? gen.pick(std::vector<std::uint64_t>{2048, 8192, 32768})
                           : 0;
    if (ArchiveCipher::isAvailable() && gen.chance(30)) {
        options.passphrase = "property-" + std::to_string(seed % 100000);
    }

    const auto files = generateFiles(gen, options.solidBlockSize);
    const auto path = directory_ / "archive.txa";

    // ---------------------------------------------------------- write
    auto writerResult = ArchiveWriter::create(path, options);
    ASSERT_TRUE(writerResult) << writerResult.error().toString();
    auto writer = std::move(writerResult).value();

    Manifest manifest;
    manifest.source.os = OsFamily::Linux;
    manifest.source.homeDirectory = "/home/property";
    manifest.source.userName = "property";

    BlockPacker packer(options.solidBlockSize, [&writer](ByteView raw) -> Result<std::uint32_t> {
        const std::uint32_t blockId = writer->nextBlockId();
        TRANSMIT_TRY(prepared, writer->prepare(blockId, raw));
        TRANSMIT_CHECK(writer->writePrepared(prepared));
        return blockId;
    });

    std::vector<BlockPacker::PlacementId> placements;
    std::vector<std::size_t> placementEntry;
    std::uint64_t nextId = 1;

    for (const GeneratedFile& file : files) {
        ManifestEntry entry;
        entry.id = nextId++;
        entry.domain = DomainId::UserData;
        entry.type = EntryType::File;
        entry.path = TokenizedPath{PathTokenId::Documents, file.path};
        entry.size = file.content.size();
        entry.modifiedUnixNs = file.modifiedUnixNs;
        entry.posix.mode = file.mode;
        entry.contentHash = Blake2b::hash256(file.content);

        if (entry.hasContent()) {
            const auto handle = packer.add(entry.contentHash, file.content);
            ASSERT_TRUE(handle) << handle.error().toString();
            placements.push_back(*handle);
            placementEntry.push_back(manifest.entries.size());
        }
        manifest.entries.push_back(entry);
    }

    ASSERT_TRUE(packer.flush());
    for (std::size_t i = 0; i < placements.size(); ++i) {
        const auto location = packer.location(placements[i]);
        ASSERT_TRUE(location) << location.error().toString();
        manifest.entries[placementEntry[i]].location = *location;
    }
    manifest.deduplicatedBytes = packer.deduplicatedBytes();
    ASSERT_TRUE(writer->finish(manifest)) << "finish";

    // A split archive is written as archive.txa.001, so the name to open is
    // whatever the writer actually produced rather than the one asked for.
    const std::vector<std::filesystem::path> parts = writer->parts();
    ASSERT_FALSE(parts.empty());
    writer.reset();

    // ----------------------------------------------------------- read
    auto readerResult = ArchiveReader::open(parts.front());
    ASSERT_TRUE(readerResult) << readerResult.error().toString();
    auto reader = std::move(readerResult).value();

    if (!options.passphrase.empty()) {
        ASSERT_TRUE(reader->isEncrypted());
        ASSERT_TRUE(reader->unlock(options.passphrase));
    }

    const auto loaded = reader->manifest();
    ASSERT_TRUE(loaded) << loaded.error().toString();
    ASSERT_EQ((*loaded)->entries.size(), files.size());

    // Every block must survive its own integrity check.
    ASSERT_TRUE(reader->verifyAllBlocks(nullptr)) << "verifyAllBlocks";

    for (std::size_t i = 0; i < files.size(); ++i) {
        const ManifestEntry& entry = (*loaded)->entries[i];
        SCOPED_TRACE("entry " + files[i].path + " (" + std::to_string(files[i].content.size()) +
                     " bytes)");

        EXPECT_EQ(entry.path.relative, files[i].path);
        EXPECT_EQ(entry.size, files[i].content.size());
        EXPECT_EQ(entry.modifiedUnixNs, files[i].modifiedUnixNs);
        EXPECT_EQ(entry.posix.mode, files[i].mode);

        // The manifest carries a content hash only for entries that have
        // content; an empty file's hash is a constant and is not worth 34
        // bytes each. Asserting the zero case as well keeps that a decision
        // rather than something a later reader might compare against by
        // accident.
        if (entry.hasContent()) {
            EXPECT_EQ(entry.contentHash, Blake2b::hash256(files[i].content));
        } else {
            EXPECT_EQ(entry.contentHash, Digest256{}) << "an empty entry should carry no hash";
        }

        const auto content = reader->readEntry(entry);
        ASSERT_TRUE(content) << content.error().toString();
        EXPECT_EQ(*content, files[i].content);
    }
}

INSTANTIATE_TEST_SUITE_P(Cases, RoundTripProperty,
                         testing::Range(0, property::caseCount(200)));

}  // namespace
}  // namespace transmit::format
