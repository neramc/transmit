#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "format/Bytes.h"
#include "format/PathToken.h"
#include "format/Result.h"
#include "format/codec/Codec.h"
#include "format/hash/Blake2b.h"

namespace transmit::format {

/// The five kinds of thing an environment capture holds. Keeping them apart in
/// the manifest lets the restore side offer them as independent choices.
enum class DomainId : std::uint8_t {
    Unknown = 0,
    UserData = 1,       ///< documents, pictures, downloads, ...
    AppState = 2,       ///< application settings, profiles and databases
    SystemSettings = 3, ///< normalised OS preferences
    Secrets = 4,        ///< opt-in credentials, always encrypted
    AppInventory = 5,   ///< the list of installed applications
};

std::string_view domainName(DomainId domain) noexcept;
Result<DomainId> domainFromName(std::string_view name);
std::vector<DomainId> allDomains();

enum class EntryType : std::uint8_t {
    File = 0,
    Directory = 1,
    Symlink = 2,
};

std::string_view entryTypeName(EntryType type) noexcept;

/// POSIX ownership and permission bits. Captured on every OS that has them and
/// applied only where the target supports them; elsewhere they are reported as
/// dropped rather than silently lost.
struct PosixMetadata {
    std::uint32_t mode = 0;
    std::uint32_t uid = 0;
    std::uint32_t gid = 0;
    std::string userName;
    std::string groupName;

    [[nodiscard]] bool isSet() const noexcept { return mode != 0; }
};

/// Windows file attribute bits (FILE_ATTRIBUTE_*).
struct WindowsMetadata {
    std::uint32_t attributes = 0;

    [[nodiscard]] bool isSet() const noexcept { return attributes != 0; }
};

/// Where an entry's bytes live inside the block stream. Small files share a
/// solid block, so several entries point into the same block at different
/// offsets.
struct BlockLocation {
    std::uint32_t blockId = 0;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;

    [[nodiscard]] bool isValid() const noexcept { return length > 0 || blockId != 0; }

    friend bool operator==(const BlockLocation& a, const BlockLocation& b) noexcept {
        return a.blockId == b.blockId && a.offset == b.offset && a.length == b.length;
    }
};

struct ManifestEntry {
    std::uint64_t id = 0;
    DomainId domain = DomainId::UserData;
    EntryType type = EntryType::File;
    TokenizedPath path;
    std::uint64_t size = 0;
    std::int64_t modifiedUnixNs = 0;
    std::int64_t createdUnixNs = 0;
    PosixMetadata posix;
    WindowsMetadata windows;
    std::string symlinkTarget;
    Digest256 contentHash{};
    BlockLocation location;

    /// Recipe that claimed this entry, for AppState entries.
    std::string appId;

    /// Set when the capture had to fall back (locked file, snapshot missing).
    std::string captureNote;

    [[nodiscard]] bool hasContent() const noexcept { return type == EntryType::File && size > 0; }
};

/// Everything about the machine the archive came from. The restore side needs
/// this to translate paths and to explain what it is looking at.
struct SourceEnvironment {
    OsFamily os = OsFamily::Unknown;
    std::string osName;         ///< "Windows 11 Pro", "Ubuntu 24.04", "macOS 14.5"
    std::string osVersion;
    std::string distroId;       ///< /etc/os-release ID, empty off Linux
    std::string desktopEnvironment;  ///< GNOME, KDE, ... empty off Linux
    std::string hostName;
    std::string userName;
    std::string homeDirectory;
    std::string appVersion;     ///< Transmit version that wrote the archive
    std::int64_t capturedUnix = 0;

    /// The known-folder table as it was on the source machine, so an absolute
    /// path found inside a config file can still be recognised at restore time.
    std::map<PathTokenId, std::string> tokenBases;
};

/// Structured data owned by a domain but interpreted above the format layer.
/// Keeping it opaque here means new domain content does not require a format
/// change: the reader hands the bytes back to whichever service understands
/// the `kind` string.
struct DomainPayload {
    DomainId domain = DomainId::Unknown;
    std::string kind;  ///< e.g. "settings.v1", "apps.v1", "secrets.v1"
    ByteBuffer data;
};

/// One compressed block in the stream. The manifest doubles as the block
/// directory: entries point at a block id, and this table turns that id into a
/// stream offset, so a restore can seek straight to what it needs.
struct BlockRecord {
    std::uint32_t blockId = 0;
    std::uint64_t streamOffset = 0;  ///< offset of the block header in the logical stream
    std::uint64_t rawSize = 0;
    std::uint64_t storedSize = 0;    ///< bytes on disk, after compression and encryption
    CodecId codec = CodecId::Zstd;
    bool encrypted = false;
};

struct Manifest {
    static constexpr std::uint32_t kCurrentVersion = 1;

    std::uint32_t version = kCurrentVersion;
    std::string archiveId;
    std::string label;
    SourceEnvironment source;
    CompressionPreset preset = CompressionPreset::Maximum;
    bool encrypted = false;

    std::vector<ManifestEntry> entries;
    std::vector<DomainPayload> payloads;
    std::vector<BlockRecord> blocks;

    /// Totals recorded at capture time so `inspect` need not walk the entries.
    std::uint64_t totalRawBytes = 0;
    std::uint64_t totalStoredBytes = 0;
    std::uint64_t deduplicatedBytes = 0;

    [[nodiscard]] std::size_t entryCountFor(DomainId domain) const;
    [[nodiscard]] std::uint64_t rawBytesFor(DomainId domain) const;
    [[nodiscard]] const DomainPayload* findPayload(DomainId domain, std::string_view kind) const;
    [[nodiscard]] const BlockRecord* findBlock(std::uint32_t blockId) const;

    [[nodiscard]] ByteBuffer serialize() const;
    static Result<Manifest> deserialize(ByteView data);
};

}  // namespace transmit::format
