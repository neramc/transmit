#include "format/FileIo.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <string>
#include <system_error>
#include <utility>

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
    return makeError(mapped, what, " '", fromFsPath(path), "': ", describeErrno(code));
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

Result<ByteBuffer> readWholeFile(const std::filesystem::path& path) {
    TRANSMIT_TRY(stream, FileStream::open(path, FileStream::Mode::Read));
    TRANSMIT_TRY(byteCount, stream.size());
    ByteBuffer buffer(static_cast<std::size_t>(byteCount));
    TRANSMIT_CHECK(stream.read(buffer));
    return buffer;
}

Status writeFileAtomically(const std::filesystem::path& path, ByteView data) {
    std::filesystem::path temporary = path;
    temporary += ".transmit-tmp";

    {
        TRANSMIT_TRY(stream, FileStream::open(temporary, FileStream::Mode::Write));
        TRANSMIT_CHECK(stream.write(data));
        TRANSMIT_CHECK(stream.flush());
    }

    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return makeError(ErrorCode::IoError, "could not replace '", fromFsPath(path),
                         "': ", ec.message());
    }
    return ok();
}

}  // namespace transmit::format
