#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "format/BlockPacker.h"
#include "format/Container.h"
#include "format/FileIo.h"

#include "support/TempDirectory.h"

namespace transmit::format {
namespace {

/// Each test gets its own directory under the build tree's temp area and
/// removes it afterwards, so a failure leaves nothing behind for the next run.
class ContainerTest : public testing::Test {
protected:
    void SetUp() override {
        directory_ = test_support::makeTemporaryDirectory("transmit-container");
    }

    void TearDown() override { test_support::removeTemporaryDirectory(directory_); }

    [[nodiscard]] std::filesystem::path archivePath(const std::string& name = "test.txa") const {
        return directory_ / name;
    }

    static ByteBuffer textBytes(std::string_view text) {
        const auto view = asBytes(text);
        return ByteBuffer(view.begin(), view.end());
    }

    static ByteBuffer repeatedBytes(std::size_t size, char fill) {
        return textBytes(std::string(size, fill));
    }

    std::filesystem::path directory_;
};

/// Builds an archive holding the given files and returns the finished manifest.
Manifest writeArchive(const std::filesystem::path& path, const ArchiveOptions& options,
                      const std::vector<std::pair<std::string, ByteBuffer>>& files,
                      std::uint64_t solidBlockSize = 4096) {
    auto writerResult = ArchiveWriter::create(path, options);
    EXPECT_TRUE(writerResult) << (writerResult ? "" : writerResult.error().toString());
    if (!writerResult) {
        return {};
    }
    auto writer = std::move(writerResult).value();

    Manifest manifest;
    manifest.source.os = OsFamily::Linux;
    manifest.source.homeDirectory = "/home/bob";
    manifest.source.userName = "bob";

    BlockPacker packer(solidBlockSize, [&writer](ByteView raw) -> Result<std::uint32_t> {
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
        entry.contentHash = Blake2b::hash256(content);

        auto handle = packer.add(entry.contentHash, content);
        EXPECT_TRUE(handle);
        handles.push_back(handle ? *handle : 0);
        manifest.entries.push_back(entry);
    }

    EXPECT_TRUE(packer.flush());

    for (std::size_t i = 0; i < handles.size(); ++i) {
        const auto location = packer.location(handles[i]);
        EXPECT_TRUE(location) << (location ? "" : location.error().toString());
        if (location) {
            manifest.entries[i].location = *location;
        }
    }
    manifest.deduplicatedBytes = packer.deduplicatedBytes();

    EXPECT_TRUE(writer->finish(manifest));
    return manifest;
}

TEST_F(ContainerTest, RoundTripsASingleFileArchive) {
    const auto path = archivePath();
    const std::vector<std::pair<std::string, ByteBuffer>> files = {
        {"a.txt", textBytes("the first file")},
        {"nested/b.txt", textBytes("the second file")},
        {"c.bin", repeatedBytes(10000, 'z')}};

    writeArchive(path, ArchiveOptions{}, files);

    auto readerResult = ArchiveReader::open(path);
    ASSERT_TRUE(readerResult) << readerResult.error().toString();
    auto reader = std::move(readerResult).value();

    EXPECT_FALSE(reader->isEncrypted());
    EXPECT_EQ(reader->partCount(), 1u);

    const auto manifest = reader->manifest();
    ASSERT_TRUE(manifest) << manifest.error().toString();
    ASSERT_EQ((*manifest)->entries.size(), files.size());
    EXPECT_EQ((*manifest)->source.homeDirectory, "/home/bob");

    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto content = reader->readEntry((*manifest)->entries[i]);
        ASSERT_TRUE(content) << content.error().toString();
        EXPECT_EQ(*content, files[i].second) << "entry " << files[i].first;
    }
}

TEST_F(ContainerTest, PreservesEmptyFiles) {
    const auto path = archivePath();
    writeArchive(path, ArchiveOptions{}, {{"empty.txt", ByteBuffer{}}});

    auto reader = std::move(ArchiveReader::open(path)).value();
    const auto manifest = reader->manifest();
    ASSERT_TRUE(manifest);
    ASSERT_EQ((*manifest)->entries.size(), 1u);

    const auto content = reader->readEntry((*manifest)->entries[0]);
    ASSERT_TRUE(content);
    EXPECT_TRUE(content->empty());
}

TEST_F(ContainerTest, SplitsAcrossVolumesAndReadsThemBack) {
    const auto path = archivePath();

    ArchiveOptions options;
    options.preset = CompressionPreset::Fast;
    // Small parts so the payload is forced across several files, exercising the
    // same boundary logic a FAT32 USB stick would.
    options.partSize = 64 * 1024;

    std::vector<std::pair<std::string, ByteBuffer>> files;
    std::mt19937 engine(7);
    std::uniform_int_distribution<unsigned int> distribution(0, 255);
    for (int i = 0; i < 8; ++i) {
        ByteBuffer content(40000);
        for (Byte& b : content) {
            b = static_cast<Byte>(distribution(engine));  // incompressible, so parts really fill
        }
        files.emplace_back("blob" + std::to_string(i) + ".bin", std::move(content));
    }

    writeArchive(path, options, files, 32768);

    // The single-file name must not exist; the parts carry the numbered suffix.
    EXPECT_FALSE(std::filesystem::exists(path));
    ASSERT_TRUE(std::filesystem::exists(partPathFor(path, 1)));
    ASSERT_TRUE(std::filesystem::exists(partPathFor(path, 2)));

    auto readerResult = ArchiveReader::open(partPathFor(path, 1));
    ASSERT_TRUE(readerResult) << readerResult.error().toString();
    auto reader = std::move(readerResult).value();
    EXPECT_GT(reader->partCount(), 1u);

    const auto manifest = reader->manifest();
    ASSERT_TRUE(manifest) << manifest.error().toString();

    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto content = reader->readEntry((*manifest)->entries[i]);
        ASSERT_TRUE(content) << content.error().toString();
        EXPECT_EQ(*content, files[i].second) << "entry " << i;
    }
}

TEST_F(ContainerTest, OpensASplitArchiveFromAnyPart) {
    const auto path = archivePath();
    ArchiveOptions options;
    options.preset = CompressionPreset::Fast;
    options.partSize = 32 * 1024;

    // Random bytes, because six runs of one repeated character compress into a
    // single part and the test would then open a set that never split.
    std::vector<std::pair<std::string, ByteBuffer>> files;
    std::mt19937 engine(5);
    std::uniform_int_distribution<unsigned int> distribution(0, 255);
    for (int i = 0; i < 6; ++i) {
        ByteBuffer content(20000);
        for (Byte& b : content) {
            b = static_cast<Byte>(distribution(engine));
        }
        files.emplace_back("f" + std::to_string(i), std::move(content));
    }
    writeArchive(path, options, files, 16384);
    ASSERT_TRUE(std::filesystem::exists(partPathFor(path, 2)));

    auto reader = ArchiveReader::open(partPathFor(path, 2));
    ASSERT_TRUE(reader) << reader.error().toString();
    EXPECT_TRUE((*reader)->manifest());
}

TEST_F(ContainerTest, ReportsAMissingVolume) {
    const auto path = archivePath();
    ArchiveOptions options;
    options.preset = CompressionPreset::Fast;
    options.partSize = 32 * 1024;

    std::vector<std::pair<std::string, ByteBuffer>> files;
    std::mt19937 engine(11);
    std::uniform_int_distribution<unsigned int> distribution(0, 255);
    for (int i = 0; i < 6; ++i) {
        ByteBuffer content(20000);
        for (Byte& b : content) {
            b = static_cast<Byte>(distribution(engine));
        }
        files.emplace_back("f" + std::to_string(i), std::move(content));
    }
    writeArchive(path, options, files, 16384);

    // Simulate the user copying only some of the parts off the USB stick.
    std::size_t partCount = 0;
    while (std::filesystem::exists(partPathFor(path, static_cast<std::uint16_t>(partCount + 1)))) {
        ++partCount;
    }
    ASSERT_GE(partCount, 2u);
    std::filesystem::remove(partPathFor(path, static_cast<std::uint16_t>(partCount)));

    const auto reader = ArchiveReader::open(partPathFor(path, 1));
    ASSERT_FALSE(reader);
    EXPECT_EQ(reader.error().code, ErrorCode::VolumeMissing);
}

TEST_F(ContainerTest, StampsEveryPartAsFinished) {
    const auto path = archivePath();
    ArchiveOptions options;
    options.preset = CompressionPreset::Fast;
    options.partSize = 32 * 1024;

    // Incompressible, so the parts are really needed: a run of one repeated
    // byte would fit in a single part however small the split size.
    std::vector<std::pair<std::string, ByteBuffer>> files;
    std::mt19937 engine(7);
    std::uniform_int_distribution<unsigned int> distribution(0, 255);
    for (int i = 0; i < 6; ++i) {
        ByteBuffer content(20000);
        for (Byte& b : content) {
            b = static_cast<Byte>(distribution(engine));
        }
        files.emplace_back("f" + std::to_string(i), std::move(content));
    }
    writeArchive(path, options, files, 16384);

    std::uint16_t index = 0;
    while (std::filesystem::exists(partPathFor(path, static_cast<std::uint16_t>(index + 1)))) {
        ++index;
        const auto raw = readWholeFile(partPathFor(path, index));
        ASSERT_TRUE(raw) << raw.error().toString();
        const auto header = VolumeHeader::decode(*raw);
        ASSERT_TRUE(header) << header.error().toString();
        EXPECT_TRUE(header->isFinalised()) << "part " << index << " was not stamped";
        EXPECT_EQ(header->partIndex, index);
    }
    ASSERT_GE(index, 2);

    // Every header agrees on the total, which is what lets a reader notice a
    // part that never made it off the stick.
    for (std::uint16_t i = 1; i <= index; ++i) {
        const auto raw = readWholeFile(partPathFor(path, i));
        ASSERT_TRUE(raw);
        const auto header = VolumeHeader::decode(*raw);
        ASSERT_TRUE(header);
        EXPECT_EQ(header->partCount, index);
    }
}

TEST_F(ContainerTest, RefusesAnArchiveWhoseWriteWasInterrupted) {
    const auto path = archivePath();
    ArchiveOptions options;
    options.preset = CompressionPreset::Fast;

    writeArchive(path, options, {{"notes.txt", textBytes("every byte of this is here")}});

    // The finished archive is perfectly readable first, so the refusal below
    // is about the stamp and nothing else.
    ASSERT_TRUE(ArchiveReader::open(path));

    // Now undo the last thing a finished write does: clear the stamp on the
    // part, exactly as a machine that lost power between the payload and the
    // finish would have left it.
    auto raw = readWholeFile(path);
    ASSERT_TRUE(raw) << raw.error().toString();
    auto header = VolumeHeader::decode(*raw);
    ASSERT_TRUE(header) << header.error().toString();
    header->flags = 0;
    header->partCount = 0;
    const auto patched = header->encode();

    auto stream = FileStream::open(path, FileStream::Mode::ReadWrite);
    ASSERT_TRUE(stream);
    ASSERT_TRUE(stream->seek(0));
    ASSERT_TRUE(stream->write(ByteView(patched)));
    stream->close();

    const auto reader = ArchiveReader::open(path);
    ASSERT_FALSE(reader) << "an unfinished archive must not read as a whole one";
    EXPECT_EQ(reader.error().code, ErrorCode::CorruptArchive);
    EXPECT_NE(reader.error().message.find("never finished"), std::string::npos)
        << reader.error().message;
}

TEST_F(ContainerTest, StillReadsAVolumeWrittenBeforeTheStampExisted) {
    const auto path = archivePath();
    ArchiveOptions options;
    options.preset = CompressionPreset::Fast;

    writeArchive(path, options, {{"notes.txt", textBytes("written by an older Transmit")}});

    // Version 1 had no finalised flag and left those bytes zero. Nothing in
    // such an archive can prove it was finished, so it has to be believed -
    // the alternative is refusing archives that are in fact fine.
    auto raw = readWholeFile(path);
    ASSERT_TRUE(raw);
    auto header = VolumeHeader::decode(*raw);
    ASSERT_TRUE(header);
    header->version = 1;
    header->flags = 0;
    const auto patched = header->encode();

    auto stream = FileStream::open(path, FileStream::Mode::ReadWrite);
    ASSERT_TRUE(stream);
    ASSERT_TRUE(stream->seek(0));
    ASSERT_TRUE(stream->write(ByteView(patched)));
    stream->close();

    const auto reader = ArchiveReader::open(path);
    ASSERT_TRUE(reader) << reader.error().toString();
    EXPECT_TRUE((*reader)->manifest());
}

TEST_F(ContainerTest, DeduplicatesIdenticalFiles) {
    const auto path = archivePath();
    const ByteBuffer shared = repeatedBytes(2000, 'd');

    const Manifest manifest =
        writeArchive(path, ArchiveOptions{},
                     {{"one.bin", shared}, {"two.bin", shared}, {"three.bin", shared}}, 65536);

    EXPECT_EQ(manifest.deduplicatedBytes, 4000u) << "two of the three copies should be free";

    auto reader = std::move(ArchiveReader::open(path)).value();
    const auto readBack = reader->manifest();
    ASSERT_TRUE(readBack);
    ASSERT_EQ((*readBack)->entries.size(), 3u);

    // All three entries point at the same bytes, and all three read back whole.
    EXPECT_EQ((*readBack)->entries[0].location.blockId, (*readBack)->entries[2].location.blockId);
    EXPECT_EQ((*readBack)->entries[0].location.offset, (*readBack)->entries[2].location.offset);
    for (const auto& entry : (*readBack)->entries) {
        const auto content = reader->readEntry(entry);
        ASSERT_TRUE(content) << content.error().toString();
        EXPECT_EQ(*content, shared);
    }
}

TEST_F(ContainerTest, EncryptsAndDecryptsWithTheRightPassphrase) {
    if (!ArchiveCipher::isAvailable()) {
        GTEST_SKIP() << "this build has no OpenSSL";
    }

    const auto path = archivePath();
    ArchiveOptions options;
    options.preset = CompressionPreset::Fast;
    options.passphrase = "correct horse battery staple";

    const std::string secretText = "the api token is hunter2";
    writeArchive(path, options, {{"secrets.txt", textBytes(secretText)}});

    // The plaintext must not be sitting in the file.
    const auto raw = readWholeFile(path);
    ASSERT_TRUE(raw);
    const std::string rawText(asText(*raw));
    EXPECT_EQ(rawText.find(secretText), std::string::npos);
    EXPECT_EQ(rawText.find("secrets.txt"), std::string::npos)
        << "the manifest is encrypted too, so file names are protected";

    auto reader = std::move(ArchiveReader::open(path)).value();
    EXPECT_TRUE(reader->isEncrypted());
    EXPECT_FALSE(reader->isUnlocked());

    const auto locked = reader->manifest();
    ASSERT_FALSE(locked) << "the manifest must stay sealed until unlocked";
    EXPECT_EQ(locked.error().code, ErrorCode::WrongPassphrase);

    ASSERT_TRUE(reader->unlock(options.passphrase));
    EXPECT_TRUE(reader->isUnlocked());

    const auto manifest = reader->manifest();
    ASSERT_TRUE(manifest) << manifest.error().toString();
    ASSERT_EQ((*manifest)->entries.size(), 1u);
    EXPECT_TRUE((*manifest)->encrypted);

    const auto content = reader->readEntry((*manifest)->entries[0]);
    ASSERT_TRUE(content);
    EXPECT_EQ(asText(*content), secretText);
}

TEST_F(ContainerTest, RejectsAWrongPassphraseImmediately) {
    if (!ArchiveCipher::isAvailable()) {
        GTEST_SKIP() << "this build has no OpenSSL";
    }

    const auto path = archivePath();
    ArchiveOptions options;
    options.preset = CompressionPreset::Fast;
    options.passphrase = "the real passphrase";
    writeArchive(path, options, {{"a.txt", textBytes("data")}});

    auto reader = std::move(ArchiveReader::open(path)).value();
    const auto status = reader->unlock("not the passphrase");
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::WrongPassphrase);
    EXPECT_FALSE(reader->isUnlocked());
}

TEST_F(ContainerTest, DetectsATamperedBlock) {
    const auto path = archivePath();
    writeArchive(path, ArchiveOptions{}, {{"a.txt", repeatedBytes(5000, 'k')}});

    // Flip a byte well past the header, inside the compressed payload.
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file.is_open());
        file.seekg(0, std::ios::end);
        const auto size = static_cast<std::size_t>(file.tellg());
        const auto target = static_cast<std::streamoff>(size / 2);
        file.seekg(target);
        char byte = 0;
        file.read(&byte, 1);
        byte = static_cast<char>(byte ^ 0xFF);
        file.seekp(target);
        file.write(&byte, 1);
    }

    auto reader = ArchiveReader::open(path);
    if (!reader) {
        // Corrupting the footer or a header is caught at open time.
        SUCCEED() << reader.error().toString();
        return;
    }

    const auto manifest = (*reader)->manifest();
    if (!manifest) {
        SUCCEED() << manifest.error().toString();
        return;
    }

    bool failed = false;
    for (const auto& entry : (*manifest)->entries) {
        if (!(*reader)->readEntry(entry)) {
            failed = true;
        }
    }
    EXPECT_TRUE(failed) << "a corrupted archive must not read back as valid data";
}

TEST_F(ContainerTest, RejectsAFileThatIsNotAnArchive) {
    const auto path = archivePath("random.bin");
    const ByteBuffer junk = repeatedBytes(500, 'x');
    ASSERT_TRUE(writeFileAtomically(path, junk));

    const auto reader = ArchiveReader::open(path);
    ASSERT_FALSE(reader);
    EXPECT_EQ(reader.error().code, ErrorCode::CorruptArchive);
}

TEST_F(ContainerTest, VerifyAllBlocksWalksTheWholeArchive) {
    const auto path = archivePath();
    std::vector<std::pair<std::string, ByteBuffer>> files;
    for (int i = 0; i < 5; ++i) {
        files.emplace_back("f" + std::to_string(i),
                           repeatedBytes(3000, static_cast<char>('a' + i)));
    }
    writeArchive(path, ArchiveOptions{}, files, 4096);

    auto reader = std::move(ArchiveReader::open(path)).value();
    std::size_t seen = 0;
    const auto status = reader->verifyAllBlocks([&seen](std::size_t done, std::size_t total) {
        EXPECT_LE(done, total);
        seen = done;
        return true;
    });
    ASSERT_TRUE(status) << status.error().toString();
    EXPECT_GT(seen, 0u);
}

TEST_F(ContainerTest, VerificationCanBeCancelled) {
    const auto path = archivePath();
    std::vector<std::pair<std::string, ByteBuffer>> files;
    for (int i = 0; i < 5; ++i) {
        files.emplace_back("f" + std::to_string(i),
                           repeatedBytes(3000, static_cast<char>('a' + i)));
    }
    writeArchive(path, ArchiveOptions{}, files, 1024);

    auto reader = std::move(ArchiveReader::open(path)).value();
    const auto status = reader->verifyAllBlocks([](std::size_t, std::size_t) { return false; });
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, ErrorCode::Cancelled);
}

TEST_F(ContainerTest, RefusesEncryptionWhenTheBuildHasNoOpenSsl) {
    if (ArchiveCipher::isAvailable()) {
        GTEST_SKIP() << "this build has OpenSSL, so the refusal path does not apply";
    }
    ArchiveOptions options;
    options.passphrase = "anything";
    const auto writer = ArchiveWriter::create(archivePath(), options);
    ASSERT_FALSE(writer);
    EXPECT_EQ(writer.error().code, ErrorCode::EncryptionUnavailable);
}

TEST_F(ContainerTest, PreparesBlocksFromManyThreadsAtOnce) {
    // The capture pipeline calls prepare() from a pool of workers on one
    // shared writer while a single thread writes the results out in order.
    // That is the only concurrency in Transmit that touches user data, so it
    // is worth checking with a tool that can see it.
    //
    // Deliberately std::thread rather than the real pipeline, which goes
    // through QtConcurrent. ThreadSanitizer cannot see synchronisation that
    // happens inside an uninstrumented library, so a run against a distribution
    // build of Qt reports races in Qt's own containers and says nothing at all
    // about this. Here every frame is instrumented and a report would be real.
    const auto path = archivePath();
    ArchiveOptions options;
    options.preset = CompressionPreset::Fast;

    auto writerResult = ArchiveWriter::create(path, options);
    ASSERT_TRUE(writerResult) << writerResult.error().toString();
    auto writer = std::move(writerResult).value();

    constexpr int kThreads = 8;
    constexpr std::size_t kBlockSize = 32 * 1024;

    std::vector<ByteBuffer> contents;
    std::vector<std::uint32_t> ids;
    for (int i = 0; i < kThreads; ++i) {
        std::mt19937 engine(static_cast<unsigned>(i) + 1);
        std::uniform_int_distribution<unsigned int> distribution(0, 255);
        ByteBuffer block(kBlockSize);
        for (Byte& b : block) {
            b = static_cast<Byte>(distribution(engine));
        }
        contents.push_back(std::move(block));
        ids.push_back(writer->nextBlockId());
    }

    std::vector<Result<PreparedBlock>> prepared(kThreads, Result<PreparedBlock>(PreparedBlock{}));
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&writer, &prepared, &contents, &ids, i] {
            prepared[static_cast<std::size_t>(i)] = writer->prepare(
                ids[static_cast<std::size_t>(i)], contents[static_cast<std::size_t>(i)]);
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    Manifest manifest;
    for (int i = 0; i < kThreads; ++i) {
        auto& block = prepared[static_cast<std::size_t>(i)];
        ASSERT_TRUE(block) << block.error().toString();
        ASSERT_TRUE(writer->writePrepared(*block));

        ManifestEntry entry;
        entry.id = static_cast<std::uint64_t>(i + 1);
        entry.type = EntryType::File;
        entry.path.token = PathTokenId::Documents;
        entry.path.relative = "block" + std::to_string(i);
        entry.size = kBlockSize;
        entry.location = BlockLocation{ids[static_cast<std::size_t>(i)], 0, kBlockSize};
        entry.contentHash = Blake2b::hash256(contents[static_cast<std::size_t>(i)]);
        manifest.entries.push_back(std::move(entry));
    }
    ASSERT_TRUE(writer->finish(manifest));
    writer.reset();

    // Every block has to come back exactly as it went in. A race that produced
    // plausible-but-wrong output would pass a "did it crash" check and fail
    // this one.
    auto reader = ArchiveReader::open(path);
    ASSERT_TRUE(reader) << reader.error().toString();
    auto loaded = (*reader)->manifest();
    ASSERT_TRUE(loaded) << loaded.error().toString();
    ASSERT_EQ((*loaded)->entries.size(), static_cast<std::size_t>(kThreads));

    for (int i = 0; i < kThreads; ++i) {
        auto content = (*reader)->readEntry((*loaded)->entries[static_cast<std::size_t>(i)]);
        ASSERT_TRUE(content) << content.error().toString();
        EXPECT_EQ(*content, contents[static_cast<std::size_t>(i)])
            << "block " << i << " came back changed";
    }
}

TEST_F(ContainerTest, ArchiveUuidRoundTripsThroughItsTextForm) {
    const ArchiveUuid uuid = generateArchiveUuid();
    const std::string text = uuidToString(uuid);
    EXPECT_EQ(text.size(), 36u);

    const auto parsed = uuidFromString(text);
    ASSERT_TRUE(parsed) << parsed.error().toString();
    EXPECT_EQ(*parsed, uuid);
    EXPECT_FALSE(uuidFromString("not-a-uuid"));
}

}  // namespace
}  // namespace transmit::format
