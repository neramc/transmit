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
                           const RetryPolicy& retry = {});

}  // namespace transmit::format
