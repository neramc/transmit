/// Opening an archive nobody wrote.
///
/// This is the whole read path: volume header, archive header, footer,
/// the manifest block, then every entry. It is what a user runs when
/// they plug in a stick, so it has to survive anything on that stick -
/// a truncated write, a corrupted sector, or a file built to be hostile.
///
/// Note what this does and does not reach. Every fixed header carries a
/// CRC-32, so random bytes are rejected at the first check and coverage
/// stays shallow: this proves the parser refuses rubbish safely, which
/// is the hostile-input case. The corrupted-media case - a valid archive
/// with a flipped bit - is covered better by flipping bits in a real
/// archive and requiring the reader to notice, which is what the
/// fault-injection suite does.

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "format/Container.h"
#include "fuzz/FuzzMain.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace transmit::format;

    // ArchiveReader::open takes a path, so the input has to reach the
    // filesystem. One reused name per process keeps this cheap; the
    // fuzzer runs it millions of times.
    static const std::filesystem::path path = [] {
        std::filesystem::path candidate =
            std::filesystem::temp_directory_path() /
            ("transmit-fuzz-" + std::to_string(static_cast<int>(::getpid())) + ".txa");
        return candidate;
    }();

    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    auto readerResult = ArchiveReader::open(path);
    if (!readerResult) {
        std::filesystem::remove(path);
        return 0;
    }
    auto reader = std::move(readerResult).value();

    // An encrypted archive without the passphrase stops here, which is
    // the point: the manifest must not be reachable without it.
    if (reader->isEncrypted()) {
        static_cast<void>(reader->unlock("fuzz"));
    }

    const auto manifest = reader->manifest();
    if (!manifest) {
        std::filesystem::remove(path);
        return 0;
    }

    // Every entry the manifest claims, read the way a restore reads it.
    // A location that points outside its block, a size that disagrees
    // with the block record, a block id that does not exist - all of
    // those have to be refusals rather than reads.
    for (const ManifestEntry& entry : (*manifest)->entries) {
        static_cast<void>(reader->readEntry(entry));
    }
    static_cast<void>(reader->verifyAllBlocks(nullptr));

    std::filesystem::remove(path);
    return 0;
}
