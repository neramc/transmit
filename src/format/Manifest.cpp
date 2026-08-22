#include "format/Manifest.h"

#include <algorithm>

#include "format/Serialization.h"

namespace transmit::format {
namespace {

// Field numbers are part of the on-disk contract: never reuse one, only append.
namespace manifest_field {
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kArchiveId = 2;
constexpr std::uint32_t kLabel = 3;
constexpr std::uint32_t kSource = 4;
constexpr std::uint32_t kPreset = 5;
constexpr std::uint32_t kEncrypted = 6;
constexpr std::uint32_t kEntry = 7;
constexpr std::uint32_t kPayload = 8;
constexpr std::uint32_t kTotalRaw = 9;
constexpr std::uint32_t kTotalStored = 10;
constexpr std::uint32_t kDeduplicated = 11;
constexpr std::uint32_t kBlock = 12;
}  // namespace manifest_field

namespace source_field {
constexpr std::uint32_t kOs = 1;
constexpr std::uint32_t kOsName = 2;
constexpr std::uint32_t kOsVersion = 3;
constexpr std::uint32_t kDistroId = 4;
constexpr std::uint32_t kDesktop = 5;
constexpr std::uint32_t kHostName = 6;
constexpr std::uint32_t kUserName = 7;
constexpr std::uint32_t kHome = 8;
constexpr std::uint32_t kAppVersion = 9;
constexpr std::uint32_t kCapturedUnix = 10;
constexpr std::uint32_t kTokenBase = 11;
}  // namespace source_field

namespace entry_field {
constexpr std::uint32_t kId = 1;
constexpr std::uint32_t kDomain = 2;
constexpr std::uint32_t kType = 3;
constexpr std::uint32_t kToken = 4;
constexpr std::uint32_t kRelative = 5;
constexpr std::uint32_t kSize = 6;
constexpr std::uint32_t kModified = 7;
constexpr std::uint32_t kCreated = 8;
constexpr std::uint32_t kMode = 9;
constexpr std::uint32_t kUid = 10;
constexpr std::uint32_t kGid = 11;
constexpr std::uint32_t kUserName = 12;
constexpr std::uint32_t kGroupName = 13;
constexpr std::uint32_t kWinAttributes = 14;
constexpr std::uint32_t kSymlinkTarget = 15;
constexpr std::uint32_t kHash = 16;
constexpr std::uint32_t kBlockId = 17;
constexpr std::uint32_t kBlockOffset = 18;
constexpr std::uint32_t kBlockLength = 19;
constexpr std::uint32_t kAppId = 20;
constexpr std::uint32_t kCaptureNote = 21;
}  // namespace entry_field

namespace block_field {
constexpr std::uint32_t kBlockId = 1;
constexpr std::uint32_t kStreamOffset = 2;
constexpr std::uint32_t kRawSize = 3;
constexpr std::uint32_t kStoredSize = 4;
constexpr std::uint32_t kCodec = 5;
constexpr std::uint32_t kEncrypted = 6;
}  // namespace block_field

namespace payload_field {
constexpr std::uint32_t kDomain = 1;
constexpr std::uint32_t kKind = 2;
constexpr std::uint32_t kData = 3;
}  // namespace payload_field

namespace token_base_field {
constexpr std::uint32_t kToken = 1;
constexpr std::uint32_t kPath = 2;
}  // namespace token_base_field

void writeSource(ByteWriter& writer, const SourceEnvironment& source) {
    writer.putUInt(source_field::kOs, static_cast<std::uint64_t>(source.os));
    writer.putString(source_field::kOsName, source.osName);
    writer.putString(source_field::kOsVersion, source.osVersion);
    writer.putString(source_field::kDistroId, source.distroId);
    writer.putString(source_field::kDesktop, source.desktopEnvironment);
    writer.putString(source_field::kHostName, source.hostName);
    writer.putString(source_field::kUserName, source.userName);
    writer.putString(source_field::kHome, source.homeDirectory);
    writer.putString(source_field::kAppVersion, source.appVersion);
    writer.putInt(source_field::kCapturedUnix, source.capturedUnix);
    for (const auto& [token, path] : source.tokenBases) {
        writer.putRecord(source_field::kTokenBase, [&](ByteWriter& nested) {
            nested.putUInt(token_base_field::kToken, static_cast<std::uint64_t>(token));
            nested.putString(token_base_field::kPath, path);
        });
    }
}

Result<SourceEnvironment> readSource(ByteView data) {
    SourceEnvironment source;
    ByteReader reader(data);
    while (!reader.atEnd()) {
        TRANSMIT_TRY(tag, reader.getTag());
        switch (tag.field) {
            case source_field::kOs: {
                TRANSMIT_TRY(value, reader.getVarint());
                source.os = static_cast<OsFamily>(value);
                break;
            }
            case source_field::kOsName: {
                TRANSMIT_TRY(value, reader.getString());
                source.osName = std::move(value);
                break;
            }
            case source_field::kOsVersion: {
                TRANSMIT_TRY(value, reader.getString());
                source.osVersion = std::move(value);
                break;
            }
            case source_field::kDistroId: {
                TRANSMIT_TRY(value, reader.getString());
                source.distroId = std::move(value);
                break;
            }
            case source_field::kDesktop: {
                TRANSMIT_TRY(value, reader.getString());
                source.desktopEnvironment = std::move(value);
                break;
            }
            case source_field::kHostName: {
                TRANSMIT_TRY(value, reader.getString());
                source.hostName = std::move(value);
                break;
            }
            case source_field::kUserName: {
                TRANSMIT_TRY(value, reader.getString());
                source.userName = std::move(value);
                break;
            }
            case source_field::kHome: {
                TRANSMIT_TRY(value, reader.getString());
                source.homeDirectory = std::move(value);
                break;
            }
            case source_field::kAppVersion: {
                TRANSMIT_TRY(value, reader.getString());
                source.appVersion = std::move(value);
                break;
            }
            case source_field::kCapturedUnix: {
                TRANSMIT_TRY(value, reader.getSignedVarint());
                source.capturedUnix = value;
                break;
            }
            case source_field::kTokenBase: {
                TRANSMIT_TRY(nestedBytes, reader.getBytes());
                ByteReader nested(nestedBytes);
                PathTokenId token = PathTokenId::Absolute;
                std::string path;
                while (!nested.atEnd()) {
                    TRANSMIT_TRY(nestedTag, nested.getTag());
                    if (nestedTag.field == token_base_field::kToken) {
                        TRANSMIT_TRY(value, nested.getVarint());
                        token = static_cast<PathTokenId>(value);
                    } else if (nestedTag.field == token_base_field::kPath) {
                        TRANSMIT_TRY(value, nested.getString());
                        path = std::move(value);
                    } else {
                        TRANSMIT_CHECK(nested.skip(nestedTag.type));
                    }
                }
                if (!path.empty()) {
                    source.tokenBases[token] = std::move(path);
                }
                break;
            }
            default:
                TRANSMIT_CHECK(reader.skip(tag.type));
                break;
        }
    }
    return source;
}

void writeEntry(ByteWriter& writer, const ManifestEntry& entry) {
    writer.putUInt(entry_field::kId, entry.id);
    writer.putUInt(entry_field::kDomain, static_cast<std::uint64_t>(entry.domain));
    writer.putUInt(entry_field::kType, static_cast<std::uint64_t>(entry.type));
    writer.putUInt(entry_field::kToken, static_cast<std::uint64_t>(entry.path.token));
    writer.putString(entry_field::kRelative, entry.path.relative);
    writer.putUInt(entry_field::kSize, entry.size);
    writer.putInt(entry_field::kModified, entry.modifiedUnixNs);
    if (entry.createdUnixNs != 0) {
        writer.putInt(entry_field::kCreated, entry.createdUnixNs);
    }
    if (entry.posix.isSet()) {
        writer.putUInt(entry_field::kMode, entry.posix.mode);
        writer.putUInt(entry_field::kUid, entry.posix.uid);
        writer.putUInt(entry_field::kGid, entry.posix.gid);
        if (!entry.posix.userName.empty()) {
            writer.putString(entry_field::kUserName, entry.posix.userName);
        }
        if (!entry.posix.groupName.empty()) {
            writer.putString(entry_field::kGroupName, entry.posix.groupName);
        }
    }
    if (entry.windows.isSet()) {
        writer.putUInt(entry_field::kWinAttributes, entry.windows.attributes);
    }
    if (!entry.symlinkTarget.empty()) {
        writer.putString(entry_field::kSymlinkTarget, entry.symlinkTarget);
    }
    if (entry.hasContent()) {
        writer.putBytes(entry_field::kHash, ByteView(entry.contentHash));
        writer.putUInt(entry_field::kBlockId, entry.location.blockId);
        writer.putUInt(entry_field::kBlockOffset, entry.location.offset);
        writer.putUInt(entry_field::kBlockLength, entry.location.length);
    }
    if (!entry.appId.empty()) {
        writer.putString(entry_field::kAppId, entry.appId);
    }
    if (!entry.captureNote.empty()) {
        writer.putString(entry_field::kCaptureNote, entry.captureNote);
    }
}

Result<ManifestEntry> readEntry(ByteView data) {
    ManifestEntry entry;
    ByteReader reader(data);
    while (!reader.atEnd()) {
        TRANSMIT_TRY(tag, reader.getTag());
        switch (tag.field) {
            case entry_field::kId: {
                TRANSMIT_TRY(value, reader.getVarint());
                entry.id = value;
                break;
            }
            case entry_field::kDomain: {
                TRANSMIT_TRY(value, reader.getVarint());
                entry.domain = static_cast<DomainId>(value);
                break;
            }
            case entry_field::kType: {
                TRANSMIT_TRY(value, reader.getVarint());
                entry.type = static_cast<EntryType>(value);
                break;
            }
            case entry_field::kToken: {
                TRANSMIT_TRY(value, reader.getVarint());
                entry.path.token = static_cast<PathTokenId>(value);
                break;
            }
            case entry_field::kRelative: {
                TRANSMIT_TRY(value, reader.getString());
                entry.path.relative = std::move(value);
                break;
            }
            case entry_field::kSize: {
                TRANSMIT_TRY(value, reader.getVarint());
                entry.size = value;
                break;
            }
            case entry_field::kModified: {
                TRANSMIT_TRY(value, reader.getSignedVarint());
                entry.modifiedUnixNs = value;
                break;
            }
            case entry_field::kCreated: {
                TRANSMIT_TRY(value, reader.getSignedVarint());
                entry.createdUnixNs = value;
                break;
            }
            case entry_field::kMode: {
                TRANSMIT_TRY(value, reader.getVarint());
                entry.posix.mode = static_cast<std::uint32_t>(value);
                break;
            }
            case entry_field::kUid: {
                TRANSMIT_TRY(value, reader.getVarint());
                entry.posix.uid = static_cast<std::uint32_t>(value);
                break;
            }
            case entry_field::kGid: {
                TRANSMIT_TRY(value, reader.getVarint());
                entry.posix.gid = static_cast<std::uint32_t>(value);
                break;
            }
            case entry_field::kUserName: {
                TRANSMIT_TRY(value, reader.getString());
                entry.posix.userName = std::move(value);
                break;
            }
            case entry_field::kGroupName: {
                TRANSMIT_TRY(value, reader.getString());
                entry.posix.groupName = std::move(value);
                break;
            }
            case entry_field::kWinAttributes: {
                TRANSMIT_TRY(value, reader.getVarint());
                entry.windows.attributes = static_cast<std::uint32_t>(value);
                break;
            }
            case entry_field::kSymlinkTarget: {
                TRANSMIT_TRY(value, reader.getString());
                entry.symlinkTarget = std::move(value);
                break;
            }
            case entry_field::kHash: {
                TRANSMIT_TRY(value, reader.getBytes());
                if (value.size() != entry.contentHash.size()) {
                    return makeError(ErrorCode::CorruptArchive, "entry hash has the wrong length");
                }
                std::copy(value.begin(), value.end(), entry.contentHash.begin());
                break;
            }
            case entry_field::kBlockId: {
                TRANSMIT_TRY(value, reader.getVarint());
                entry.location.blockId = static_cast<std::uint32_t>(value);
                break;
            }
            case entry_field::kBlockOffset: {
                TRANSMIT_TRY(value, reader.getVarint());
                entry.location.offset = value;
                break;
            }
            case entry_field::kBlockLength: {
                TRANSMIT_TRY(value, reader.getVarint());
                entry.location.length = value;
                break;
            }
            case entry_field::kAppId: {
                TRANSMIT_TRY(value, reader.getString());
                entry.appId = std::move(value);
                break;
            }
            case entry_field::kCaptureNote: {
                TRANSMIT_TRY(value, reader.getString());
                entry.captureNote = std::move(value);
                break;
            }
            default:
                TRANSMIT_CHECK(reader.skip(tag.type));
                break;
        }
    }
    return entry;
}

}  // namespace

std::string_view domainName(DomainId domain) noexcept {
    switch (domain) {
        case DomainId::UserData:       return "userdata";
        case DomainId::AppState:       return "appstate";
        case DomainId::SystemSettings: return "settings";
        case DomainId::Secrets:        return "secrets";
        case DomainId::AppInventory:   return "apps";
        case DomainId::Unknown:        return "unknown";
    }
    return "unknown";
}

Result<DomainId> domainFromName(std::string_view name) {
    if (name == "userdata") return DomainId::UserData;
    if (name == "appstate") return DomainId::AppState;
    if (name == "settings") return DomainId::SystemSettings;
    if (name == "secrets")  return DomainId::Secrets;
    if (name == "apps")     return DomainId::AppInventory;
    return makeError(ErrorCode::InvalidArgument,
                     "unknown domain '", std::string(name),
                     "' (expected userdata, appstate, settings, secrets or apps)");
}

std::vector<DomainId> allDomains() {
    return {DomainId::UserData, DomainId::AppState, DomainId::SystemSettings, DomainId::Secrets,
            DomainId::AppInventory};
}

std::string_view entryTypeName(EntryType type) noexcept {
    switch (type) {
        case EntryType::File:      return "file";
        case EntryType::Directory: return "directory";
        case EntryType::Symlink:   return "symlink";
    }
    return "file";
}

std::size_t Manifest::entryCountFor(DomainId domain) const {
    return static_cast<std::size_t>(
        std::count_if(entries.begin(), entries.end(),
                      [domain](const ManifestEntry& entry) { return entry.domain == domain; }));
}

std::uint64_t Manifest::rawBytesFor(DomainId domain) const {
    std::uint64_t total = 0;
    for (const auto& entry : entries) {
        if (entry.domain == domain && entry.type == EntryType::File) {
            total += entry.size;
        }
    }
    return total;
}

const DomainPayload* Manifest::findPayload(DomainId domain, std::string_view kind) const {
    for (const auto& payload : payloads) {
        if (payload.domain == domain && payload.kind == kind) {
            return &payload;
        }
    }
    return nullptr;
}

const BlockRecord* Manifest::findBlock(std::uint32_t blockId) const {
    const auto it = std::find_if(blocks.begin(), blocks.end(), [blockId](const BlockRecord& block) {
        return block.blockId == blockId;
    });
    return it == blocks.end() ? nullptr : &*it;
}

ByteBuffer Manifest::serialize() const {
    ByteBuffer buffer;
    // Entries dominate the size; reserving up front avoids repeated growth on
    // captures with hundreds of thousands of files.
    buffer.reserve(entries.size() * 96 + 4096);

    ByteWriter writer(buffer);
    writer.putUInt(manifest_field::kVersion, version);
    writer.putString(manifest_field::kArchiveId, archiveId);
    writer.putString(manifest_field::kLabel, label);
    writer.putRecord(manifest_field::kSource,
                     [this](ByteWriter& nested) { writeSource(nested, source); });
    writer.putUInt(manifest_field::kPreset, static_cast<std::uint64_t>(preset));
    writer.putBool(manifest_field::kEncrypted, encrypted);
    writer.putUInt(manifest_field::kTotalRaw, totalRawBytes);
    writer.putUInt(manifest_field::kTotalStored, totalStoredBytes);
    writer.putUInt(manifest_field::kDeduplicated, deduplicatedBytes);

    for (const auto& entry : entries) {
        writer.putRecord(manifest_field::kEntry,
                         [&entry](ByteWriter& nested) { writeEntry(nested, entry); });
    }
    for (const auto& block : blocks) {
        writer.putRecord(manifest_field::kBlock, [&block](ByteWriter& nested) {
            nested.putUInt(block_field::kBlockId, block.blockId);
            nested.putUInt(block_field::kStreamOffset, block.streamOffset);
            nested.putUInt(block_field::kRawSize, block.rawSize);
            nested.putUInt(block_field::kStoredSize, block.storedSize);
            nested.putUInt(block_field::kCodec, static_cast<std::uint64_t>(block.codec));
            nested.putBool(block_field::kEncrypted, block.encrypted);
        });
    }
    for (const auto& payload : payloads) {
        writer.putRecord(manifest_field::kPayload, [&payload](ByteWriter& nested) {
            nested.putUInt(payload_field::kDomain, static_cast<std::uint64_t>(payload.domain));
            nested.putString(payload_field::kKind, payload.kind);
            nested.putBytes(payload_field::kData, payload.data);
        });
    }
    return buffer;
}

Result<Manifest> Manifest::deserialize(ByteView data) {
    Manifest manifest;
    ByteReader reader(data);

    while (!reader.atEnd()) {
        TRANSMIT_TRY(tag, reader.getTag());
        switch (tag.field) {
            case manifest_field::kVersion: {
                TRANSMIT_TRY(value, reader.getVarint());
                manifest.version = static_cast<std::uint32_t>(value);
                if (manifest.version > kCurrentVersion) {
                    return makeError(ErrorCode::UnsupportedVersion,
                                     "this archive was written by a newer Transmit (manifest v",
                                     std::to_string(manifest.version), ")");
                }
                break;
            }
            case manifest_field::kArchiveId: {
                TRANSMIT_TRY(value, reader.getString());
                manifest.archiveId = std::move(value);
                break;
            }
            case manifest_field::kLabel: {
                TRANSMIT_TRY(value, reader.getString());
                manifest.label = std::move(value);
                break;
            }
            case manifest_field::kSource: {
                TRANSMIT_TRY(bytes, reader.getBytes());
                TRANSMIT_TRY(source, readSource(bytes));
                manifest.source = std::move(source);
                break;
            }
            case manifest_field::kPreset: {
                TRANSMIT_TRY(value, reader.getVarint());
                manifest.preset = static_cast<CompressionPreset>(value);
                break;
            }
            case manifest_field::kEncrypted: {
                TRANSMIT_TRY(value, reader.getBool());
                manifest.encrypted = value;
                break;
            }
            case manifest_field::kTotalRaw: {
                TRANSMIT_TRY(value, reader.getVarint());
                manifest.totalRawBytes = value;
                break;
            }
            case manifest_field::kTotalStored: {
                TRANSMIT_TRY(value, reader.getVarint());
                manifest.totalStoredBytes = value;
                break;
            }
            case manifest_field::kDeduplicated: {
                TRANSMIT_TRY(value, reader.getVarint());
                manifest.deduplicatedBytes = value;
                break;
            }
            case manifest_field::kEntry: {
                TRANSMIT_TRY(bytes, reader.getBytes());
                TRANSMIT_TRY(entry, readEntry(bytes));
                manifest.entries.push_back(std::move(entry));
                break;
            }
            case manifest_field::kBlock: {
                TRANSMIT_TRY(bytes, reader.getBytes());
                ByteReader nested(bytes);
                BlockRecord block;
                while (!nested.atEnd()) {
                    TRANSMIT_TRY(nestedTag, nested.getTag());
                    switch (nestedTag.field) {
                        case block_field::kBlockId: {
                            TRANSMIT_TRY(value, nested.getVarint());
                            block.blockId = static_cast<std::uint32_t>(value);
                            break;
                        }
                        case block_field::kStreamOffset: {
                            TRANSMIT_TRY(value, nested.getVarint());
                            block.streamOffset = value;
                            break;
                        }
                        case block_field::kRawSize: {
                            TRANSMIT_TRY(value, nested.getVarint());
                            block.rawSize = value;
                            break;
                        }
                        case block_field::kStoredSize: {
                            TRANSMIT_TRY(value, nested.getVarint());
                            block.storedSize = value;
                            break;
                        }
                        case block_field::kCodec: {
                            TRANSMIT_TRY(value, nested.getVarint());
                            block.codec = static_cast<CodecId>(value);
                            break;
                        }
                        case block_field::kEncrypted: {
                            TRANSMIT_TRY(value, nested.getBool());
                            block.encrypted = value;
                            break;
                        }
                        default:
                            TRANSMIT_CHECK(nested.skip(nestedTag.type));
                            break;
                    }
                }
                manifest.blocks.push_back(block);
                break;
            }
            case manifest_field::kPayload: {
                TRANSMIT_TRY(bytes, reader.getBytes());
                ByteReader nested(bytes);
                DomainPayload payload;
                while (!nested.atEnd()) {
                    TRANSMIT_TRY(nestedTag, nested.getTag());
                    switch (nestedTag.field) {
                        case payload_field::kDomain: {
                            TRANSMIT_TRY(value, nested.getVarint());
                            payload.domain = static_cast<DomainId>(value);
                            break;
                        }
                        case payload_field::kKind: {
                            TRANSMIT_TRY(value, nested.getString());
                            payload.kind = std::move(value);
                            break;
                        }
                        case payload_field::kData: {
                            TRANSMIT_TRY(value, nested.getBytes());
                            payload.data.assign(value.begin(), value.end());
                            break;
                        }
                        default:
                            TRANSMIT_CHECK(nested.skip(nestedTag.type));
                            break;
                    }
                }
                manifest.payloads.push_back(std::move(payload));
                break;
            }
            default:
                TRANSMIT_CHECK(reader.skip(tag.type));
                break;
        }
    }
    return manifest;
}

}  // namespace transmit::format
