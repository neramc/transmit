// Deliberate breakage.
//
// Everything here arranges a failure that is real, common on removable media,
// and close to impossible to reproduce on demand: a stick that fills up half
// way through, one that is pulled out, one that accepts a write and stores
// something shorter, one that hands back a byte other than the one it was
// given, a copy that stopped part way. None of these are exotic - they are
// what the failure reports of every backup tool are made of - and all of them
// are silent unless something is looking.
//
// The standard each of them is held to is the same: Transmit may fail, and it
// may fail loudly, but it may never report success over data that is not what
// went in.

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "format/BlockPacker.h"
#include "format/Container.h"
#include "format/FileIo.h"
#include "format/IoHooks.h"
#include "format/Manifest.h"

namespace transmit::format {
namespace {

/// How many single-bit flips are tried over a valid archive. Large enough that
/// a hole in the checking shows up rather than hiding behind luck; small enough
/// that the suite still runs in a few seconds.
constexpr int kBitFlipSamples = 2000;

/// Fixed, so a failure here is reproducible from the log rather than being
/// something that happened once on somebody's machine.
constexpr std::uint64_t kSeed = 0x1a2b3c4d5e6f7788ull;

class FaultInjectionTest : public testing::Test {
protected:
    void SetUp() override {
        directory_ = std::filesystem::temp_directory_path() /
                     ("transmit-fault-" + std::to_string(counter_++));
        std::filesystem::remove_all(directory_);
        std::filesystem::create_directories(directory_);
    }

    void TearDown() override {
        setIoHooks(nullptr);
        std::error_code ec;
        std::filesystem::remove_all(directory_, ec);
    }

    [[nodiscard]] std::filesystem::path archivePath(const std::string& name = "fault.txa") const {
        return directory_ / name;
    }

    std::filesystem::path directory_;
    static inline int counter_ = 0;
};

struct Fixture {
    std::vector<std::pair<std::string, ByteBuffer>> files;
    Manifest manifest;
};

ByteBuffer patternedBytes(std::size_t size, std::uint64_t seed) {
    std::mt19937_64 engine(seed);
    ByteBuffer content(size);
    for (Byte& b : content) {
        b = static_cast<Byte>(engine() & 0xFFu);
    }
    return content;
}

/// A small archive with a mixture of content: compressible text, incompressible
/// noise, an empty file, and a duplicate so the deduplication path is exercised
/// too. Written with a small block size so several blocks exist to damage.
Fixture writeFixture(const std::filesystem::path& path, std::uint64_t partSize = 0) {
    Fixture fixture;
    fixture.files.emplace_back("notes.txt", [] {
        const std::string text(4000, 'n');
        const auto view = asBytes(text);
        return ByteBuffer(view.begin(), view.end());
    }());
    fixture.files.emplace_back("noise.bin", patternedBytes(9000, 1));
    fixture.files.emplace_back("empty.dat", ByteBuffer{});
    fixture.files.emplace_back("more-noise.bin", patternedBytes(7000, 2));
    fixture.files.emplace_back("copy-of-noise.bin", patternedBytes(9000, 1));

    ArchiveOptions options;
    options.preset = CompressionPreset::Fast;
    options.solidBlockSize = 4096;
    options.partSize = partSize;

    auto writerResult = ArchiveWriter::create(path, options);
    EXPECT_TRUE(writerResult) << (writerResult ? "" : writerResult.error().toString());
    auto writer = std::move(writerResult).value();

    BlockPacker packer(options.solidBlockSize, [&writer](ByteView raw) -> Result<std::uint32_t> {
        return writer->writeBlock(raw);
    });

    std::vector<BlockPacker::PlacementId> placements;
    for (const auto& [name, content] : fixture.files) {
        auto placed = packer.add(content);
        EXPECT_TRUE(placed);
        placements.push_back(*placed);
    }
    EXPECT_TRUE(packer.flush());

    for (std::size_t i = 0; i < fixture.files.size(); ++i) {
        ManifestEntry entry;
        entry.id = static_cast<std::uint64_t>(i + 1);
        entry.type = EntryType::File;
        entry.path.token = PathTokenId::Documents;
        entry.path.relative = fixture.files[i].first;
        entry.size = fixture.files[i].second.size();
        if (!fixture.files[i].second.empty()) {
            const auto location = packer.location(placements[i]);
            EXPECT_TRUE(location);
            entry.location = *location;
            entry.contentHash = Blake2b::hash256(fixture.files[i].second);
        }
        fixture.manifest.entries.push_back(std::move(entry));
    }

    EXPECT_TRUE(writer->finish(fixture.manifest));
    return fixture;
}

/// What happened when a damaged archive was read all the way through.
enum class ReadOutcome {
    Refused,        ///< an error was reported: the best possible answer
    CameBackWhole,  ///< read cleanly and every byte matched, so nothing was lost
    Wrong,          ///< read cleanly and handed back something else. Never allowed.
};

ReadOutcome readEverything(const std::filesystem::path& path, const Fixture& fixture) {
    auto reader = ArchiveReader::open(path);
    if (!reader) {
        return ReadOutcome::Refused;
    }
    auto manifest = (*reader)->manifest();
    if (!manifest) {
        return ReadOutcome::Refused;
    }
    if ((*manifest)->entries.size() != fixture.manifest.entries.size()) {
        // A manifest that parsed but describes a different archive is not a
        // clean read; it is the reader having been lied to and believed it.
        return ReadOutcome::Wrong;
    }

    for (std::size_t i = 0; i < (*manifest)->entries.size(); ++i) {
        const ManifestEntry& entry = (*manifest)->entries[i];
        const ByteBuffer& expected = fixture.files[i].second;

        if (entry.path.relative != fixture.files[i].first || entry.size != expected.size()) {
            return ReadOutcome::Wrong;
        }
        auto content = (*reader)->readEntry(entry);
        if (!content) {
            return ReadOutcome::Refused;
        }
        if (*content != expected) {
            return ReadOutcome::Wrong;
        }
    }
    return ReadOutcome::CameBackWhole;
}

std::vector<char> readFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(stream), {});
}

void writeFileBytes(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// ------------------------------------------------------------ corruption

TEST_F(FaultInjectionTest, NoSingleBitFlipEverProducesWrongData) {
    const auto original = archivePath();
    const Fixture fixture = writeFixture(original);
    const std::vector<char> sound = readFileBytes(original);
    ASSERT_FALSE(sound.empty());

    std::mt19937_64 engine(kSeed);
    std::uniform_int_distribution<std::size_t> bytePicker(0, sound.size() - 1);
    std::uniform_int_distribution<int> bitPicker(0, 7);

    const auto damaged = archivePath("damaged.txa");
    int refused = 0;
    std::vector<std::size_t> unprotected;

    for (int sample = 0; sample < kBitFlipSamples; ++sample) {
        const std::size_t index = bytePicker(engine);
        const int bit = bitPicker(engine);

        std::vector<char> broken = sound;
        broken[index] = static_cast<char>(static_cast<unsigned char>(broken[index]) ^
                                          (1u << static_cast<unsigned>(bit)));
        writeFileBytes(damaged, broken);

        const ReadOutcome outcome = readEverything(damaged, fixture);
        ASSERT_NE(outcome, ReadOutcome::Wrong)
            << "flipping bit " << bit << " of byte " << index
            << " changed the data that came back and nothing said so (seed " << kSeed << ", sample "
            << sample << ")";
        if (outcome == ReadOutcome::Refused) {
            ++refused;
        } else {
            unprotected.push_back(index);
        }
    }

    std::printf("[ bit flips ] %d of %d refused outright, %zu read back byte-identical\n", refused,
                kBitFlipSamples, unprotected.size());

    // The assertion above is the one that must never move: no fault may
    // produce data that reads back clean and wrong. It held for every one of
    // these on every platform.
    //
    // This second one is about coverage rather than safety. A flip that reads
    // back byte-identical did no harm, but it did land somewhere nothing
    // checks - a reserved field, the padding after a checksum - and a large
    // number of them would mean a whole region had lost its protection.
    //
    // A budget rather than an equality. The compressed bytes differ between
    // platforms (a different zstd build packs the same input differently), so
    // which offsets are padding differs too: this was an equality, it passed on
    // Linux with 2000 of 2000, and macOS found four. Four in two thousand is
    // padding; a region losing its checksum would be hundreds.
    constexpr double kUnprotectedBudget = 0.01;
    const auto allowed = static_cast<std::size_t>(kBitFlipSamples * kUnprotectedBudget);
    if (unprotected.size() > allowed) {
        std::string offsets;
        for (std::size_t i = 0; i < unprotected.size() && i < 40; ++i) {
            offsets += " " + std::to_string(unprotected[i]);
        }
        ADD_FAILURE() << unprotected.size() << " of " << kBitFlipSamples
                      << " flips read back clean, above the " << allowed
                      << " expected of reserved bytes and padding. The archive is " << sound.size()
                      << " bytes; the offsets that were not caught are:" << offsets;
    }
}

TEST_F(FaultInjectionTest, NoAmountOfTruncationEverProducesWrongData) {
    const auto original = archivePath();
    const Fixture fixture = writeFixture(original);
    const std::vector<char> sound = readFileBytes(original);
    ASSERT_GT(sound.size(), 100u);

    const auto cut = archivePath("cut.txa");
    for (int percent = 1; percent < 100; ++percent) {
        const auto keep = sound.size() * static_cast<std::size_t>(percent) / 100;
        writeFileBytes(cut, std::vector<char>(sound.begin(),
                                              sound.begin() + static_cast<std::ptrdiff_t>(keep)));

        const ReadOutcome outcome = readEverything(cut, fixture);
        EXPECT_EQ(outcome, ReadOutcome::Refused)
            << "an archive cut off at " << percent << "% was not refused";
    }
}

TEST_F(FaultInjectionTest, ADamagedPartIsRefusedRatherThanPartlyRead) {
    const auto original = archivePath();
    const Fixture fixture = writeFixture(original, 8192);

    // The set has to have really split, or this is testing the single-file
    // path under another name.
    ASSERT_TRUE(std::filesystem::exists(partPathFor(original, 2)));

    std::uint16_t partCount = 0;
    while (
        std::filesystem::exists(partPathFor(original, static_cast<std::uint16_t>(partCount + 1)))) {
        ++partCount;
    }

    for (std::uint16_t part = 1; part <= partCount; ++part) {
        const auto path = partPathFor(original, part);
        const std::vector<char> sound = readFileBytes(path);

        // Halfway through each part, which for part 1 is inside the payload
        // rather than the header - the header has its own checksum and is the
        // easy case.
        std::vector<char> broken = sound;
        broken[broken.size() / 2] =
            static_cast<char>(static_cast<unsigned char>(broken[broken.size() / 2]) ^ 0xFFu);
        writeFileBytes(path, broken);

        EXPECT_NE(readEverything(partPathFor(original, 1), fixture), ReadOutcome::Wrong)
            << "damage in part " << part << " came back as data";

        writeFileBytes(path, sound);
    }
}

// ---------------------------------------------------------- write faults

TEST_F(FaultInjectionTest, RunningOutOfSpaceStopsTheWriteAndSaysSo) {
    IoHooks hooks;
    std::uint64_t allowed = 6000;
    hooks.beforeWrite = [&allowed](const std::filesystem::path&, std::uint64_t,
                                   std::size_t size) -> std::optional<Error> {
        if (size > allowed) {
            Error full(ErrorCode::IoError, "no space left on device");
            full.systemCode = ENOSPC;
            return full;
        }
        allowed -= size;
        return std::nullopt;
    };
    const ScopedIoHooks installed(std::move(hooks));

    const auto path = archivePath("full.txa");
    ArchiveOptions options;
    options.preset = CompressionPreset::Fast;
    options.solidBlockSize = 4096;

    auto writerResult = ArchiveWriter::create(path, options);
    ASSERT_TRUE(writerResult) << writerResult.error().toString();
    auto writer = std::move(writerResult).value();

    // Somewhere in here the disk fills up. Wherever that is, it has to be
    // reported rather than swallowed.
    bool reported = false;
    Manifest manifest;
    for (int i = 0; i < 12 && !reported; ++i) {
        const ByteBuffer block = patternedBytes(4096, static_cast<std::uint64_t>(i));
        if (!writer->writeBlock(block)) {
            reported = true;
        }
    }
    if (!reported && !writer->finish(manifest)) {
        reported = true;
    }
    EXPECT_TRUE(reported) << "the archive filled the disk and said it had worked";
}

TEST_F(FaultInjectionTest, AShortWriteIsReportedRatherThanIgnored) {
    IoHooks hooks;
    bool shortened = false;
    hooks.writeLimit = [&shortened](const std::filesystem::path&, std::size_t size) -> std::size_t {
        // The first block payload takes all but one byte, which is what a
        // device with just too little room left does. The threshold clears the
        // 96-byte archive header on purpose: failing there would prove the
        // header write is checked and say nothing about the payload, which is
        // where all the data is.
        if (!shortened && size > 1000) {
            shortened = true;
            return size - 1;
        }
        return size;
    };
    const ScopedIoHooks installed(std::move(hooks));

    const auto path = archivePath("short.txa");
    ArchiveOptions options;
    options.preset = CompressionPreset::Fast;
    options.solidBlockSize = 4096;

    auto writerResult = ArchiveWriter::create(path, options);
    ASSERT_TRUE(writerResult) << writerResult.error().toString();
    auto writer = std::move(writerResult).value();

    Manifest manifest;
    bool reported = false;
    for (int i = 0; i < 4 && !reported; ++i) {
        // Incompressible, so the block really is written at its full size and
        // the shortening lands on payload rather than on a few hundred bytes
        // of compressed output.
        if (!writer->writeBlock(patternedBytes(4096, static_cast<std::uint64_t>(i)))) {
            reported = true;
        }
    }
    if (!reported && !writer->finish(manifest)) {
        reported = true;
    }
    EXPECT_TRUE(shortened) << "the hook never fired, so this proved nothing";
    EXPECT_TRUE(reported) << "one byte went missing and the write called itself a success";
}

// ----------------------------------------------------------- read faults

TEST_F(FaultInjectionTest, AReadThatFailsOnceIsTriedAgain) {
    const auto path = archivePath();
    const Fixture fixture = writeFixture(path);

    IoHooks hooks;
    int failuresLeft = 1;
    hooks.beforeRead = [&failuresLeft](const std::filesystem::path&, std::uint64_t,
                                       std::size_t) -> std::optional<Error> {
        if (failuresLeft > 0) {
            --failuresLeft;
            Error flaky(ErrorCode::IoError, "a bad read off marginal media");
            flaky.systemCode = EIO;
            return flaky;
        }
        return std::nullopt;
    };
    const ScopedIoHooks installed(std::move(hooks));

    EXPECT_EQ(readEverything(path, fixture), ReadOutcome::CameBackWhole)
        << "one flaky read was treated as final";
    EXPECT_EQ(failuresLeft, 0) << "the hook never fired, so this proved nothing";
}

TEST_F(FaultInjectionTest, AReadThatKeepsFailingIsReported) {
    const auto path = archivePath();
    const Fixture fixture = writeFixture(path);

    IoHooks hooks;
    int fired = 0;
    hooks.beforeRead = [&fired](const std::filesystem::path&, std::uint64_t,
                                std::size_t) -> std::optional<Error> {
        ++fired;
        Error flaky(ErrorCode::IoError, "this sector is gone");
        flaky.systemCode = EIO;
        return flaky;
    };
    const ScopedIoHooks installed(std::move(hooks));

    EXPECT_EQ(readEverything(path, fixture), ReadOutcome::Refused);
    EXPECT_GT(fired, 1) << "a read that could have been retried was not";
}

TEST_F(FaultInjectionTest, AStickPulledOutMidReadIsNotRetried) {
    const auto path = archivePath();
    const Fixture fixture = writeFixture(path);

    IoHooks hooks;
    int fired = 0;
    hooks.beforeRead = [&fired](const std::filesystem::path&, std::uint64_t,
                                std::size_t) -> std::optional<Error> {
        ++fired;
        Error gone(ErrorCode::IoError, "no such device");
        gone.systemCode = ENODEV;
        return gone;
    };
    const ScopedIoHooks installed(std::move(hooks));

    EXPECT_EQ(readEverything(path, fixture), ReadOutcome::Refused);
    EXPECT_EQ(fired, 1) << "an unplugged drive was asked again, which only wastes the user's time";
}

}  // namespace
}  // namespace transmit::format
