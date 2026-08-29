#include "format/ChecksumSidecar.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <string_view>

#include "format/FileIo.h"
#include "format/hash/Md5.h"

namespace transmit::format {
namespace {

/// Big enough that the read syscalls disappear against the hashing, small
/// enough that hashing a part never depends on how much memory is free.
constexpr std::size_t kReadChunk = 1024 * 1024;

std::string nowInUtc() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm parts{};
#if defined(_WIN32)
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    char text[32] = {};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &parts);
    return text;
}

/// `md5sum` writes the name as it was given on the command line. Everything
/// here lives next to the sidecar, so the name alone is right - and it is what
/// makes `md5sum -c` work when somebody has moved the whole folder.
std::string fileNameOf(const std::filesystem::path& path) {
    return path.filename().string();
}

bool isHexDigit(char letter) {
    return (letter >= '0' && letter <= '9') || (letter >= 'a' && letter <= 'f') ||
           (letter >= 'A' && letter <= 'F');
}

int hexValue(char letter) {
    if (letter >= '0' && letter <= '9') {
        return letter - '0';
    }
    if (letter >= 'a' && letter <= 'f') {
        return letter - 'a' + 10;
    }
    return letter - 'A' + 10;
}

}  // namespace

Result<Digest128> md5OfFile(const std::filesystem::path& path) {
    TRANSMIT_TRY(stream, FileStream::open(path, FileStream::Mode::Read));
    TRANSMIT_TRY(total, stream.size());

    Md5 md5;
    ByteBuffer buffer(kReadChunk);
    std::uint64_t remaining = total;
    while (remaining > 0) {
        const std::size_t take =
            remaining < kReadChunk ? static_cast<std::size_t>(remaining) : kReadChunk;
        TRANSMIT_CHECK(stream.read(MutableByteView(buffer.data(), take)));
        md5.update(ByteView(buffer.data(), take));
        remaining -= take;
    }
    return md5.finish128();
}

Result<std::filesystem::path> writeChecksumSidecar(const std::filesystem::path& sidecarPath,
                                                   const std::vector<std::filesystem::path>& parts,
                                                   const Manifest& manifest,
                                                   const SidecarOptions& options) {
    std::string text;
    text.reserve(options.includeEntries ? manifest.entries.size() * 80 + 512 : 512);

    text += "# Transmit archive checksums\n";
    if (!options.archiveName.empty()) {
        text += "# archive: " + options.archiveName + "\n";
    }
    if (!manifest.archiveId.empty()) {
        text += "# id: " + manifest.archiveId + "\n";
    }
    text += "# written: " + nowInUtc() + "\n";
    text += "#\n";
    text += "# Check a copy of this archive with:\n";
    text += "#     md5sum -c " + fileNameOf(sidecarPath) + "\n";
    text += "# run in the folder holding the parts. The lines beginning with a\n";
    text += "# hash are comments and md5sum skips them.\n";
    text += "#\n";

    // The parts first: these are the lines md5sum acts on, and a checker that
    // gives up part way through should have done the useful work already.
    for (const std::filesystem::path& part : parts) {
        TRANSMIT_TRY(digest, md5OfFile(part));
        text += toHex(digest);
        text += "  ";
        text += fileNameOf(part);
        text += "\n";
    }

    if (options.includeEntries) {
        text += "#\n";
        text += "# The files inside the archive. Not files on disk, so these are\n";
        text += "# comments: they are here to be read, and to say which file is\n";
        text += "# wrong when one of them is.\n";
        for (const ManifestEntry& entry : manifest.entries) {
            if (!entry.hasContent() || !entry.hasMd5()) {
                continue;
            }
            text += "# entry ";
            text += toHex(entry.contentMd5);
            text += ' ';
            text += std::to_string(entry.size);
            text += ' ';
            text += entry.path.toDisplayString();
            text += '\n';
        }
    }

    TRANSMIT_CHECK(writeFileAtomically(sidecarPath, asBytes(text)));
    return sidecarPath;
}

Result<std::vector<SidecarPart>> readChecksumSidecar(const std::filesystem::path& sidecarPath) {
    TRANSMIT_TRY(stream, FileStream::open(sidecarPath, FileStream::Mode::Read));
    TRANSMIT_TRY(size, stream.size());

    std::string text(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        TRANSMIT_CHECK(stream.read(
            MutableByteView(reinterpret_cast<Byte*>(text.data()), static_cast<std::size_t>(size))));
    }

    std::vector<SidecarPart> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string_view line(text.data() + start,
                              (end == std::string::npos ? text.size() : end) - start);
        start = (end == std::string::npos ? text.size() : end) + 1;

        if (line.empty() || line.front() == '#') {
            continue;
        }
        // "<32 hex>  <name>", exactly as md5sum writes it. A line that is not
        // that shape is skipped rather than failing the read: a person may
        // well have added a note of their own, and refusing to read the file
        // because of it would be the least useful possible response.
        if (line.size() < 35 ||
            !std::all_of(line.begin(), line.begin() + 32,
                         [](const char letter) { return isHexDigit(letter); })) {
            continue;
        }

        SidecarPart part;
        for (std::size_t i = 0; i < part.md5.size(); ++i) {
            part.md5[i] =
                static_cast<Byte>((hexValue(line[i * 2]) << 4) | hexValue(line[i * 2 + 1]));
        }
        std::size_t nameAt = 32;
        while (nameAt < line.size() && (line[nameAt] == ' ' || line[nameAt] == '*')) {
            ++nameAt;
        }
        part.fileName = std::string(line.substr(nameAt));
        if (!part.fileName.empty()) {
            parts.push_back(std::move(part));
        }
    }
    return parts;
}

}  // namespace transmit::format
