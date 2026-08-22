#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

#include "format/Bytes.h"
#include "format/Result.h"

namespace transmit::format {

/// Converts between UTF-8 strings and std::filesystem::path. On Windows a
/// narrow std::string is interpreted in the active code page, which mangles
/// non-ASCII names, so the conversion always goes through char8_t.
std::filesystem::path toFsPath(std::string_view utf8);
std::string fromFsPath(const std::filesystem::path& path);

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
    void close();

private:
    std::FILE* handle_ = nullptr;
    std::filesystem::path path_;
};

/// Reads a whole file. Intended for small files (recipes, reports); the
/// capture pipeline streams instead.
Result<ByteBuffer> readWholeFile(const std::filesystem::path& path);

/// Writes to a sibling temporary file and renames over the target, so an
/// interrupted write cannot leave a half-written report or catalog behind.
Status writeFileAtomically(const std::filesystem::path& path, ByteView data);

}  // namespace transmit::format
