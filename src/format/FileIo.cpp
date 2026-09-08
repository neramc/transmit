#include "format/FileIo.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <string>
#include <system_error>
#include <utility>

#include "format/IoHooks.h"

#if defined(_WIN32)
#include <windows.h>

#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/falloc.h>
#endif
#endif

namespace transmit::format {
namespace {

/// The message for an errno value, without std::strerror.
///
/// std::strerror returns a pointer to a shared buffer, so two threads failing
/// at once can read each other's message - and this layer is used from the
/// compression workers. MSVC deprecates it outright for the same reason.
std::string describeErrno(int code) {
    constexpr std::size_t kMessageSize = 256;
    std::array<char, kMessageSize> buffer{};

#if defined(_WIN32)
    if (::strerror_s(buffer.data(), buffer.size(), code) != 0) {
        return "unknown error";
    }
    return buffer.data();
#elif defined(__GLIBC__) && defined(_GNU_SOURCE)
    // The GNU flavour returns the message, which may or may not be the buffer.
    return ::strerror_r(code, buffer.data(), buffer.size());
#else
    if (::strerror_r(code, buffer.data(), buffer.size()) != 0) {
        return "unknown error";
    }
    return buffer.data();
#endif
}

Error errnoError(const std::filesystem::path& path, const char* what) {
    const int code = errno;
    ErrorCode mapped = ErrorCode::IoError;
    if (code == ENOENT) {
        mapped = ErrorCode::NotFound;
    } else if (code == EACCES || code == EPERM) {
        mapped = ErrorCode::PermissionDenied;
    } else if (code == ENOMEM) {
        mapped = ErrorCode::OutOfMemory;
    }
    Error error = makeError(mapped, what, " '", fromFsPath(path), "': ", describeErrno(code));
    error.systemCode = code;
    return error;
}

#if !defined(_WIN32)
// Windows opens through _wfopen_s with a wide mode string, so this exists only
// for the other branch - and a function defined for nobody is a warning.
const char* modeString(FileStream::Mode mode) {
    switch (mode) {
        case FileStream::Mode::Read:
            return "rb";
        case FileStream::Mode::Write:
            return "wb";
        case FileStream::Mode::ReadWrite:
            return "r+b";
    }
    return "rb";
}
#endif

}  // namespace

bool isTransient(const Error& error) noexcept {
    // Cancellation is a decision, not a fault, and the archive-level failures
    // describe bytes that are already wrong - reading them again gives the
    // same wrong bytes.
    switch (error.code) {
        case ErrorCode::PermissionDenied:
        case ErrorCode::NotFound:
        case ErrorCode::EndOfStream:
        case ErrorCode::CorruptArchive:
        case ErrorCode::UnsupportedVersion:
        case ErrorCode::UnsupportedCodec:
        case ErrorCode::IntegrityMismatch:
        case ErrorCode::WrongPassphrase:
        case ErrorCode::EncryptionUnavailable:
        case ErrorCode::InvalidArgument:
        case ErrorCode::Cancelled:
            return false;
        default:
            break;
    }
    if (error.systemCode == 0) {
        return false;
    }

    // errno on every platform, including Windows. FileStream is built on
    // stdio, which sets errno rather than the Win32 last-error, so comparing
    // against ERROR_CRC and its neighbours - as this did - was comparing a
    // value from one numbering space against constants from another. EIO is 5
    // on Windows; so is ERROR_ACCESS_DENIED. Every retry test failed there and
    // was right to.
    //
    // Anything that ever stores a Win32 code here must translate it first.
    switch (error.systemCode) {
        case EIO:     // one bad read; often fine on the next pass
        case EINTR:   // a signal arrived mid-call
        case EAGAIN:  // would block
        case EBUSY:   // the device is doing something else
#if defined(ETIMEDOUT)
        case ETIMEDOUT:
#endif
#if defined(ENOBUFS)
        case ENOBUFS:
#endif
            return true;

        // Deliberately absent, because a second attempt cannot change any of
        // them and pretending otherwise wastes the user's time at exactly the
        // moment they need a straight answer: ENOSPC and EDQUOT (nowhere to
        // put it), EROFS (nowhere to put it, permanently), ENODEV and ENXIO
        // (the stick is gone), EFBIG (too large for this filesystem).
        default:
            return false;
    }
}

std::filesystem::path toFsPath(std::string_view utf8) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string fromFsPath(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

FileStream::~FileStream() {
    close();
}

FileStream::FileStream(FileStream&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)), path_(std::move(other.path_)) {}

FileStream& FileStream::operator=(FileStream&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
        path_ = std::move(other.path_);
    }
    return *this;
}

Result<FileStream> FileStream::open(const std::filesystem::path& path, Mode mode) {
    FileStream stream;
#if defined(_WIN32)
    std::FILE* handle = nullptr;
    const std::wstring wideMode = [mode] {
        switch (mode) {
            case Mode::Read:
                return std::wstring(L"rb");
            case Mode::Write:
                return std::wstring(L"wb");
            case Mode::ReadWrite:
                return std::wstring(L"r+b");
        }
        return std::wstring(L"rb");
    }();
    if (::_wfopen_s(&handle, path.c_str(), wideMode.c_str()) != 0 || handle == nullptr) {
        return errnoError(path, "could not open");
    }
#else
    std::FILE* handle = std::fopen(path.c_str(), modeString(mode));
    if (handle == nullptr) {
        return errnoError(path, "could not open");
    }
#endif
    stream.handle_ = handle;
    stream.path_ = path;
    return stream;
}

void FileStream::close() {
    if (handle_ != nullptr) {
        std::fclose(handle_);
        handle_ = nullptr;
    }
}

Status FileStream::read(MutableByteView out) {
    if (handle_ == nullptr) {
        return makeError(ErrorCode::IoError, "read from a closed file");
    }
    if (out.empty()) {
        return ok();
    }
    if (const IoHooks* hooks = ioHooks(); hooks != nullptr && hooks->beforeRead) {
        const auto position = tell();
        if (auto injected = hooks->beforeRead(path_, position ? *position : 0, out.size())) {
            return *injected;
        }
    }
    const std::size_t got = std::fread(out.data(), 1, out.size(), handle_);
    if (got != out.size()) {
        if (std::feof(handle_) != 0) {
            return makeError(ErrorCode::EndOfStream, "unexpected end of '", fromFsPath(path_), "'");
        }
        return errnoError(path_, "could not read");
    }
    return ok();
}

Status FileStream::write(ByteView data) {
    if (handle_ == nullptr) {
        return makeError(ErrorCode::IoError, "write to a closed file");
    }
    if (data.empty()) {
        return ok();
    }
    std::size_t offered = data.size();
    if (const IoHooks* hooks = ioHooks(); hooks != nullptr) {
        if (hooks->beforeWrite) {
            const auto position = tell();
            if (auto injected = hooks->beforeWrite(path_, position ? *position : 0, data.size())) {
                return *injected;
            }
        }
        if (hooks->writeLimit) {
            offered = std::min(offered, hooks->writeLimit(path_, data.size()));
        }
    }
    const std::size_t put = std::fwrite(data.data(), 1, offered, handle_);
    if (put != data.size()) {
        // A device that is nearly full takes some of what it was offered and
        // stops without raising an error. Reporting that as a success is how an
        // archive ends up short by exactly the amount nobody noticed.
        //
        // ferror decides which of the two happened. errno cannot: it is only
        // meaningful when the call actually failed, and reading it after a
        // short write reports whatever unrelated thing set it last.
        if (std::ferror(handle_) != 0) {
            return errnoError(path_, "could not write");
        }
        return makeError(ErrorCode::IoError, "only ", std::to_string(put), " of ",
                         std::to_string(data.size()), " bytes could be written to '",
                         fromFsPath(path_), "'");
    }
    return ok();
}

Status FileStream::seek(std::uint64_t offset) {
    if (handle_ == nullptr) {
        return makeError(ErrorCode::IoError, "seek on a closed file");
    }
#if defined(_WIN32)
    if (::_fseeki64(handle_, static_cast<std::int64_t>(offset), SEEK_SET) != 0) {
#else
    if (::fseeko(handle_, static_cast<off_t>(offset), SEEK_SET) != 0) {
#endif
        return errnoError(path_, "could not seek in");
    }
    return ok();
}

Result<std::uint64_t> FileStream::tell() const {
    if (handle_ == nullptr) {
        return makeError(ErrorCode::IoError, "tell on a closed file");
    }
#if defined(_WIN32)
    const std::int64_t position = ::_ftelli64(handle_);
#else
    const off_t position = ::ftello(handle_);
#endif
    if (position < 0) {
        return errnoError(path_, "could not query the position in");
    }
    return static_cast<std::uint64_t>(position);
}

Result<std::uint64_t> FileStream::size() const {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path_, ec);
    if (ec) {
        return makeError(ErrorCode::IoError, "could not size '", fromFsPath(path_),
                         "': ", ec.message());
    }
    return static_cast<std::uint64_t>(bytes);
}

Status FileStream::flush() {
    if (handle_ != nullptr && std::fflush(handle_) != 0) {
        return errnoError(path_, "could not flush");
    }
    return ok();
}

Status FileStream::sync() {
    if (handle_ == nullptr) {
        return ok();
    }
    // The stdio buffer has to go first: syncing the descriptor says nothing
    // about bytes still sitting in this process.
    TRANSMIT_CHECK(flush());

#if defined(_WIN32)
    const int descriptor = ::_fileno(handle_);
    if (descriptor < 0) {
        return errnoError(path_, "could not sync");
    }
    const auto native = reinterpret_cast<HANDLE>(::_get_osfhandle(descriptor));
    if (native == INVALID_HANDLE_VALUE || ::FlushFileBuffers(native) == 0) {
        return makeError(ErrorCode::IoError, "could not sync '", fromFsPath(path_), "'");
    }
    return ok();
#else
    const int descriptor = ::fileno(handle_);
    if (descriptor < 0) {
        return errnoError(path_, "could not sync");
    }
#if defined(__APPLE__)
    // fsync on macOS only reaches the drive's own write cache. F_FULLFSYNC is
    // the one that asks the drive to commit to the platter, and it is the
    // reason a Mac survives a power cut where the same code on Linux does not.
    // Some filesystems - network mounts especially - refuse it; a plain fsync
    // is the best that is on offer there.
    if (::fcntl(descriptor, F_FULLFSYNC) != -1) {
        return ok();
    }
    if (errno != ENOTSUP && errno != EINVAL && errno != ENOTTY) {
        return errnoError(path_, "could not sync");
    }
    if (::fsync(descriptor) != 0) {
        return errnoError(path_, "could not sync");
    }
#elif defined(__linux__)
    // fdatasync skips the metadata flush when only the size changed, which is
    // the common case here and measurably cheaper on rotational media.
    if (::fdatasync(descriptor) != 0) {
        return errnoError(path_, "could not sync");
    }
#else
    if (::fsync(descriptor) != 0) {
        return errnoError(path_, "could not sync");
    }
#endif
    return ok();
#endif
}

Status syncDirectory(const std::filesystem::path& directory) {
#if defined(_WIN32)
    (void)directory;
    return ok();
#else
    const int descriptor = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor < 0) {
        // A read-only mount or a filesystem that will not hand out directory
        // descriptors is not a reason to fail the write that just succeeded.
        if (errno == EACCES || errno == EPERM || errno == EINVAL || errno == ENOTDIR) {
            return ok();
        }
        return errnoError(directory, "could not open");
    }
    const int synced = ::fsync(descriptor);
    const int syncErrno = errno;
    ::close(descriptor);
    if (synced != 0) {
        // Several filesystems - and every one that has no directory metadata
        // to speak of - answer EINVAL here. The rename is still ordered.
        if (syncErrno == EINVAL || syncErrno == ENOTSUP) {
            return ok();
        }
        errno = syncErrno;
        return errnoError(directory, "could not sync");
    }
    return ok();
#endif
}

Status FileStream::truncate(std::uint64_t length) {
    if (handle_ == nullptr) {
        return makeError(ErrorCode::IoError, "cannot resize a file that is not open");
    }
    // Anything still in the stdio buffer belongs to a position the resize is
    // about to move, so it goes out first.
    TRANSMIT_CHECK(flush());

#if defined(_WIN32)
    const int descriptor = ::_fileno(handle_);
    if (descriptor < 0) {
        return errnoError(path_, "could not resize");
    }
    if (::_chsize_s(descriptor, static_cast<__int64>(length)) != 0) {
        return errnoError(path_, "could not resize");
    }
#else
    const int descriptor = ::fileno(handle_);
    if (descriptor < 0) {
        return errnoError(path_, "could not resize");
    }
    if (::ftruncate(descriptor, static_cast<off_t>(length)) != 0) {
        return errnoError(path_, "could not resize");
    }
#endif
    return ok();
}

Status FileStream::punchHole(std::uint64_t offset, std::uint64_t length) {
    if (handle_ == nullptr || length == 0) {
        return ok();
    }
    TRANSMIT_CHECK(flush());

#if defined(_WIN32)
    // Nothing to do. A file marked sparse gets its holes from the seek itself,
    // and on a filesystem that refused the mark this would write the zeroes
    // out rather than take them back - the slow way round to the same bytes.
    (void)offset;
    return ok();
#else
    const int descriptor = ::fileno(handle_);
    if (descriptor < 0) {
        return ok();
    }

    // Both calls want whole filesystem blocks, so the hole is trimmed inward
    // to them. Trimming the other way would give back a block that holds real
    // data at one end of it.
    struct stat about {};
    const auto blockSize =
        static_cast<std::uint64_t>(::fstat(descriptor, &about) == 0 && about.st_blksize > 0
                                       ? static_cast<std::uint64_t>(about.st_blksize)
                                       : 4096U);

    const std::uint64_t first = ((offset + blockSize - 1) / blockSize) * blockSize;
    const std::uint64_t last = ((offset + length) / blockSize) * blockSize;
    if (last <= first) {
        return ok();  // not a whole block of it, so there is nothing to give back
    }

#if defined(__APPLE__)
    fpunchhole_t hole{};
    hole.fp_offset = static_cast<off_t>(first);
    hole.fp_length = static_cast<off_t>(last - first);
    (void)::fcntl(descriptor, F_PUNCHHOLE, &hole);
#elif defined(__linux__)
    (void)::fallocate(descriptor, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                      static_cast<off_t>(first), static_cast<off_t>(last - first));
#else
    (void)first;
    (void)last;
#endif
    // Deliberately unchecked: see the header. Every way this fails leaves the
    // file holding exactly the bytes it should.
    return ok();
#endif
}

Status FileStream::declareSparse() {
#if defined(_WIN32)
    if (handle_ == nullptr) {
        return makeError(ErrorCode::IoError, "cannot mark a file that is not open");
    }
    const int descriptor = ::_fileno(handle_);
    if (descriptor < 0) {
        return errnoError(path_, "could not mark as sparse");
    }
    const auto native = reinterpret_cast<HANDLE>(::_get_osfhandle(descriptor));
    if (native == INVALID_HANDLE_VALUE) {
        return errnoError(path_, "could not mark as sparse");
    }
    DWORD returned = 0;
    if (::DeviceIoControl(native, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) ==
        0) {
        // FAT32 and exFAT have no holes to give, and a network redirector may
        // refuse. Neither is a failure: the zeroes get written instead and the
        // file reads back the same, which is the whole reason this is allowed
        // to be a preference rather than a requirement.
        const DWORD error = ::GetLastError();
        if (error != ERROR_INVALID_FUNCTION && error != ERROR_NOT_SUPPORTED &&
            error != ERROR_INVALID_PARAMETER) {
            return makeError(ErrorCode::IoError, "could not mark '", fromFsPath(path_),
                             "' as sparse");
        }
    }
#endif
    return ok();
}

std::vector<ByteRange> spansWorthWriting(ByteView data, std::uint64_t smallestHole) {
    std::vector<ByteRange> spans;
    if (data.empty()) {
        return spans;
    }
    if (smallestHole == 0) {
        smallestHole = 1;
    }

    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    const std::size_t total = data.size();
    const auto hole = static_cast<std::size_t>(
        std::min<std::uint64_t>(smallestHole, static_cast<std::uint64_t>(total) + 1));

    std::size_t spanStart = 0;
    std::size_t at = 0;
    while (at < total) {
        if (bytes[at] != 0) {
            ++at;
            continue;
        }
        // A zero. Measure the run before deciding anything: a run shorter than
        // a hole is cheaper to write than to skip, and skipping it would split
        // one span into two for no gain.
        std::size_t runEnd = at;
        while (runEnd < total && bytes[runEnd] == 0) {
            ++runEnd;
        }
        if (runEnd - at >= hole) {
            if (at > spanStart) {
                spans.push_back(ByteRange{spanStart, at - spanStart});
            }
            spanStart = runEnd;
        }
        at = runEnd;
    }
    if (total > spanStart) {
        spans.push_back(ByteRange{spanStart, total - spanStart});
    }
    return spans;
}

Result<std::uint64_t> allocatedSize(const std::filesystem::path& path) {
#if defined(_WIN32)
    ULARGE_INTEGER size{};
    size.LowPart = ::GetCompressedFileSizeW(path.c_str(), &size.HighPart);
    if (size.LowPart == INVALID_FILE_SIZE && ::GetLastError() != NO_ERROR) {
        return makeError(ErrorCode::IoError, "could not measure '", fromFsPath(path), "'");
    }
    return static_cast<std::uint64_t>(size.QuadPart);
#else
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) {
        return errnoError(path, "could not measure");
    }
    // st_blocks is in 512-byte units by definition, whatever the filesystem's
    // own block size is.
    return static_cast<std::uint64_t>(info.st_blocks) * 512U;
#endif
}

Result<ByteBuffer> readWholeFile(const std::filesystem::path& path, const RetryPolicy& retry) {
    return withRetry(retry, [&path]() -> Result<ByteBuffer> {
        TRANSMIT_TRY(stream, FileStream::open(path, FileStream::Mode::Read));
        TRANSMIT_TRY(byteCount, stream.size());
        ByteBuffer buffer(static_cast<std::size_t>(byteCount));
        TRANSMIT_CHECK(stream.read(buffer));
        return buffer;
    });
}

namespace {

/// One attempt at the swap. Separated so withRetry can repeat it whole: the
/// temporary is created fresh each time, so a second attempt starts from the
/// same place the first did.
/// Writes `data`, leaving the long runs of zeroes in it as holes.
///
/// The gaps are made by seeking rather than by punching afterwards, which is
/// the one approach all three systems agree on: a write landing past the end
/// of the file leaves a hole behind it on every filesystem that has them, and
/// zeroes on every filesystem that does not. Either way the bytes read back
/// the same, so this is safe to attempt anywhere.
///
/// The length is set at the end rather than trusted to the last write, because
/// a file that ends in zeroes has nothing written after its final span and
/// would otherwise arrive short by exactly that run.
Status writeSpansWithHoles(FileStream& stream, ByteView data) {
    TRANSMIT_CHECK(stream.declareSparse());

    const std::vector<ByteRange> spans = spansWorthWriting(data);
    for (const ByteRange& span : spans) {
        TRANSMIT_CHECK(stream.seek(span.offset));
        TRANSMIT_CHECK(stream.write(data.subspan(static_cast<std::size_t>(span.offset),
                                                 static_cast<std::size_t>(span.length))));
    }
    TRANSMIT_CHECK(stream.truncate(static_cast<std::uint64_t>(data.size())));

    // And then ask for the gaps back, because not every filesystem left them
    // empty. APFS fills a skipped region in, so on macOS the zeroes are
    // written and then handed back; on ext4 and on NTFS the hole is already
    // there and this finds nothing to do.
    std::uint64_t after = 0;
    for (const ByteRange& span : spans) {
        if (span.offset > after) {
            TRANSMIT_CHECK(stream.punchHole(after, span.offset - after));
        }
        after = span.offset + span.length;
    }
    if (static_cast<std::uint64_t>(data.size()) > after) {
        TRANSMIT_CHECK(stream.punchHole(after, static_cast<std::uint64_t>(data.size()) - after));
    }
    return ok();
}

Status writeFileOnce(const std::filesystem::path& path, ByteView data, Durability durability,
                     Sparseness sparseness) {
    std::filesystem::path temporary = path;
    temporary += ".transmit-tmp";

    {
        TRANSMIT_TRY(stream, FileStream::open(temporary, FileStream::Mode::Write));
        auto cleanUp = [&temporary] {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        };
        const bool worthScanning = sparseness == Sparseness::PunchHoles &&
                                   static_cast<std::uint64_t>(data.size()) >= kSmallestSparseFile;
        if (auto status = worthScanning ? writeSpansWithHoles(stream, data) : stream.write(data);
            !status) {
            cleanUp();
            return status;
        }
        // The bytes have to reach the device before the rename, or a power cut
        // leaves the new name pointing at a file of zeroes - which is worse
        // than the half-written file this function exists to prevent.
        if (auto status = durability == Durability::Buffered ? stream.flush() : stream.sync();
            !status) {
            cleanUp();
            return status;
        }
    }

    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return makeError(ErrorCode::IoError, "could not replace '", fromFsPath(path),
                         "': ", ec.message());
    }

    if (durability == Durability::DataAndName) {
        const std::filesystem::path parent = path.parent_path();
        TRANSMIT_CHECK(syncDirectory(parent.empty() ? std::filesystem::path(".") : parent));
    }
    return ok();
}

}  // namespace

Status writeFileAtomically(const std::filesystem::path& path, ByteView data, Durability durability,
                           Sparseness sparseness, const RetryPolicy& retry) {
    return withRetry(retry,
                     [&]() -> Status { return writeFileOnce(path, data, durability, sparseness); });
}

Result<bool> dropFromPageCache(const std::filesystem::path& path) {
#if defined(__linux__)
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0) {
        return errnoError(path, "could not open");
    }
    // Length zero means "to the end of the file". Only clean pages go, which
    // is why the caller has to have synced first.
    const int result = ::posix_fadvise(descriptor, 0, 0, POSIX_FADV_DONTNEED);
    ::close(descriptor);
    if (result != 0) {
        // posix_fadvise returns the error rather than setting errno, so it has
        // to be put back for the shared helper to read.
        errno = result;
        return errnoError(path, "could not drop the cache for");
    }
    return true;
#else
    // macOS has F_NOCACHE, which stops a descriptor caching what it reads from
    // now on but does not evict what is already there; Windows needs the file
    // reopened with FILE_FLAG_NO_BUFFERING and its alignment rules. Neither is
    // eviction, and claiming otherwise would make the report a lie.
    (void)path;
    return false;
#endif
}

}  // namespace transmit::format
