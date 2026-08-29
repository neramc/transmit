#include "format/FileIo.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>

#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
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

#if defined(_WIN32)
    switch (error.systemCode) {
        case 21:    // ERROR_NOT_READY - the drive is still coming up
        case 23:    // ERROR_CRC - a bad read off marginal media
        case 32:    // ERROR_SHARING_VIOLATION
        case 33:    // ERROR_LOCK_VIOLATION
        case 121:   // ERROR_SEM_TIMEOUT
        case 1117:  // ERROR_IO_DEVICE
            return true;
        default:
            return false;
    }
#else
    switch (error.systemCode) {
        case EIO:     // one bad read; often fine on the next pass
        case EINTR:   // a signal arrived mid-call
        case EAGAIN:  // would block
        case EBUSY:   // the device is doing something else
        case ETIMEDOUT:
        case ENOBUFS:
            return true;

        // Deliberately absent, because a second attempt cannot change any of
        // them and pretending otherwise wastes the user's time at exactly the
        // moment they need a straight answer: ENOSPC and EDQUOT (nowhere to
        // put it), EROFS (nowhere to put it, permanently), ENODEV and ENXIO
        // (the stick is gone), EFBIG (too large for this filesystem).
        default:
            return false;
    }
#endif
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
    const std::size_t put = std::fwrite(data.data(), 1, data.size(), handle_);
    if (put != data.size()) {
        return errnoError(path_, "could not write");
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
Status writeFileOnce(const std::filesystem::path& path, ByteView data, Durability durability) {
    std::filesystem::path temporary = path;
    temporary += ".transmit-tmp";

    {
        TRANSMIT_TRY(stream, FileStream::open(temporary, FileStream::Mode::Write));
        auto cleanUp = [&temporary] {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        };
        if (auto status = stream.write(data); !status) {
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
                           const RetryPolicy& retry) {
    return withRetry(retry, [&]() -> Status { return writeFileOnce(path, data, durability); });
}

}  // namespace transmit::format
