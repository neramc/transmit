#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "format/Bytes.h"
#include "format/Result.h"

namespace transmit::format {

/// Converts between UTF-8 strings and std::filesystem::path. On Windows a
/// narrow std::string is interpreted in the active code page, which mangles
/// non-ASCII names, so the conversion always goes through char8_t.
std::filesystem::path toFsPath(std::string_view utf8);
std::string fromFsPath(const std::filesystem::path& path);

/// Whether trying the same operation again could plausibly get a different
/// answer.
///
/// A marginal USB connection, a stick that is still spinning up, a sector that
/// reads correctly on the second pass: those are worth another attempt, and on
/// removable media they are the common case rather than the exotic one. A full
/// disk, a file that belongs to somebody else, a device that has been
/// unplugged: those will answer the same way for ever, and retrying them costs
/// the user time and hides the real problem behind a delay.
[[nodiscard]] bool isTransient(const Error& error) noexcept;

/// How hard to try. The defaults are for removable media, where a second
/// attempt is cheap and often works; `once()` is for anything that must fail
/// immediately.
struct RetryPolicy {
    int attempts = 3;  ///< total, including the first
    std::chrono::milliseconds firstDelay{25};
    std::chrono::milliseconds maximumDelay{400};

    [[nodiscard]] static RetryPolicy once() noexcept {
        return RetryPolicy{1, std::chrono::milliseconds{0}, std::chrono::milliseconds{0}};
    }
};

/// Repeats `operation` while it fails in a way another attempt might fix,
/// backing off between tries. Anything else - success, or a failure that
/// repeating cannot help - is returned at once.
///
/// `operation` must be safe to run more than once. That rules out anything
/// that has already consumed part of a stream; whole-file reads and writes
/// that stage into a temporary are fine, and are what this is used for.
template<typename Fn>
auto withRetry(const RetryPolicy& policy, Fn&& operation) -> decltype(operation()) {
    std::chrono::milliseconds delay = policy.firstDelay;
    for (int attempt = 1;; ++attempt) {
        auto result = operation();
        if (result || attempt >= policy.attempts || !isTransient(result.error())) {
            return result;
        }
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
            delay = std::min(delay * 2, policy.maximumDelay);
        }
    }
}

/// Thin RAII wrapper over stdio with Result-based errors and 64-bit offsets.
/// stdio is used rather than iostreams because it gives direct control over
/// buffering, which matters when streaming multi-gigabyte volumes to USB.
class FileStream {
public:
    enum class Mode { Read, Write, ReadWrite };

    FileStream() = default;
    ~FileStream();

    FileStream(const FileStream&) = delete;
    FileStream& operator=(const FileStream&) = delete;
    FileStream(FileStream&& other) noexcept;
    FileStream& operator=(FileStream&& other) noexcept;

    static Result<FileStream> open(const std::filesystem::path& path, Mode mode);

    [[nodiscard]] bool isOpen() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    Status read(MutableByteView out);
    Status write(ByteView data);
    Status seek(std::uint64_t offset);
    Result<std::uint64_t> tell() const;
    Result<std::uint64_t> size() const;
    Status flush();

    /// Sets the file's length, growing it with a hole rather than with bytes.
    ///
    /// Needed because seeking past the end of a file and writing nothing does
    /// not make it longer: a file that ends in a run of zeroes would otherwise
    /// come back short by exactly that run.
    Status truncate(std::uint64_t length);

    /// Gives back the disk behind a region, leaving a hole that reads as
    /// zeroes.
    ///
    /// The same idea in three places under three names: fallocate on Linux,
    /// F_PUNCHHOLE on macOS, FSCTL_SET_ZERO_DATA on Windows.
    ///
    /// Needed because not every filesystem makes a hole out of a write that
    /// lands past the end of the file. Linux does, and NTFS does once the file
    /// has been marked sparse - but APFS fills the gap in, so on macOS the
    /// zeroes have to be handed back afterwards rather than never written.
    ///
    /// Only whole filesystem blocks are given back; a region is trimmed inward
    /// to them, so a request smaller than one block does nothing at all. That
    /// keeps the three systems saying the same thing - macOS refuses an
    /// unaligned region outright, while the other two accept one and zero the
    /// bytes at its edges - and it means this can never take away data that
    /// happened to share a block with the hole.
    ///
    /// Refusals are success. A filesystem with no holes to give, a region too
    /// small to align, a network mount that will not: in every one of those
    /// the file already holds the right bytes, and failing a restore over how
    /// much room they take would be the wrong trade.
    Status punchHole(std::uint64_t offset, std::uint64_t length);

    /// Tells the filesystem this file is allowed to have holes in it.
    ///
    /// Only Windows needs asking: NTFS zero-fills a seek unless the file has
    /// been marked sparse first, and the mark can only be set while the file
    /// is empty. Every POSIX filesystem that supports holes makes one on its
    /// own when a write lands past the end, so there this succeeds without
    /// doing anything. A filesystem that has no holes to offer - FAT32 and
    /// exFAT on a stick, most obviously - is not an error either: it writes
    /// the zeroes, and the file reads back the same either way.
    Status declareSparse();

    /// Pushes everything written so far all the way to the device, so it
    /// survives losing power - flush() only hands the bytes to the operating
    /// system, which may hold them in the page cache for half a minute.
    ///
    /// One call costs milliseconds on a USB stick, so it belongs at commit
    /// points and at bounded intervals, never in the write loop.
    Status sync();

    void close();

private:
    std::FILE* handle_ = nullptr;
    std::filesystem::path path_;
};

/// Makes a directory's own contents durable: after a rename, the new name
/// itself lives in the parent directory, and syncing the file does not save
/// it. Without this a power cut can leave the target missing even though its
/// bytes reached the disk.
///
/// A no-op on Windows, where a directory handle cannot be flushed and NTFS
/// orders the metadata itself.
Status syncDirectory(const std::filesystem::path& directory);

/// Reads a whole file. Intended for small files (recipes, reports); the
/// capture pipeline streams instead. Retried, because reading the same file
/// twice cannot do any harm and once is not always enough on a USB stick.
Result<ByteBuffer> readWholeFile(const std::filesystem::path& path, const RetryPolicy& retry = {});

/// A run of bytes inside a file.
struct ByteRange {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;

    [[nodiscard]] friend bool operator==(const ByteRange& a, const ByteRange& b) noexcept {
        return a.offset == b.offset && a.length == b.length;
    }
};

/// The shortest run of zeroes worth leaving as a hole.
///
/// A hole costs a seek and, on Windows, an entry in the file's allocated-range
/// table; below a few filesystem blocks that is more expensive than writing
/// the zeroes. 64 KiB is comfortably above the largest block size in ordinary
/// use and small enough that the runs inside a disk image are all found.
inline constexpr std::uint64_t kSmallestHole = 64 * 1024;

/// Files below this are always written whole.
///
/// Scanning costs a pass over the bytes, and no file this small has enough
/// hole in it to pay for one. Disk images, virtual machines and database files
/// - the things that are mostly hole - are all far above it.
inline constexpr std::uint64_t kSmallestSparseFile = 1024 * 1024;

/// The parts of `data` worth writing, leaving out every run of zeroes at least
/// `smallestHole` long.
///
/// The gaps between the returned ranges are the holes. A file made only of
/// zeroes yields no ranges at all, which is correct and is why the caller
/// still has to set the length afterwards.
[[nodiscard]] std::vector<ByteRange> spansWorthWriting(ByteView data,
                                                       std::uint64_t smallestHole = kSmallestHole);

/// How many bytes the filesystem has actually put behind `path`.
///
/// Smaller than the file's length exactly when it has holes in it, which is
/// what makes this the way to check that a hole survived rather than being
/// quietly filled in. Rounded up to a whole number of blocks, so it is never
/// exactly the length of the data either.
[[nodiscard]] Result<std::uint64_t> allocatedSize(const std::filesystem::path& path);

/// Whether a write may leave holes where the data is zeroes.
enum class Sparseness {
    /// Every byte is written. What a small file wants, and the default.
    Dense,
    /// Long runs of zeroes are skipped rather than written, where the file is
    /// big enough for that to be worth doing.
    ///
    /// The file that arrives is byte for byte the same either way - a hole
    /// reads as zeroes - so this is never a question of what the data is, only
    /// of how much room it takes. It matters most on a restore: a disk image
    /// that occupied 3 GB on the machine it came from needs its full 40 GB on
    /// the far side otherwise, and the restore fails on a disk that was ample.
    PunchHoles,
};

/// How far a write is pushed before it is called done.
enum class Durability {
    /// Buffered. Survives this process dying, not the machine losing power.
    Buffered,
    /// The bytes reach the device before the rename, so the target is either
    /// the old file or the whole new one - never a hole. The new name itself
    /// may still be pending, so after a power cut the file can be missing.
    Data,
    /// The bytes and the name both reach the device. The strongest, and the
    /// only one that costs a directory flush per file; a caller writing many
    /// files into one folder wants Data plus one syncDirectory at the end.
    DataAndName,
};

/// Writes to a sibling temporary file and renames over the target, so an
/// interrupted write cannot leave a half-written report or catalog behind.
Status writeFileAtomically(const std::filesystem::path& path, ByteView data,
                           Durability durability = Durability::DataAndName,
                           Sparseness sparseness = Sparseness::Dense,
                           const RetryPolicy& retry = {});

/// Asks the operating system to forget its cached copy of a file.
///
/// Verification that reads back what is still in the page cache is not
/// verification: it proves the bytes the writer produced, not the bytes the
/// drive kept. Dropping the cache first is what makes reading it back mean
/// something.
///
/// Returns true when the cache was actually dropped, false when this system
/// has no way to ask - which is not an error, but it is something the report
/// has to say out loud rather than let somebody assume otherwise. Linux can do
/// it with posix_fadvise; macOS and Windows have no per-file eviction short of
/// reopening with caching disabled, so they answer false.
///
/// The file must already have been synced: eviction only discards pages that
/// have been written back, so an unsynced file quietly stays cached.
Result<bool> dropFromPageCache(const std::filesystem::path& path);

}  // namespace transmit::format
