#include "format/TransferJournal.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "format/JournalFile.h"
#include "format/Serialization.h"
#include "format/hash/Blake2b.h"
#include "format/hash/Crc32.h"

namespace transmit::format {
namespace {

/// "TXAJ": the capture journal, as against the restore journal's "TXAR".
constexpr JournalFormat kFormat{{'T', 'X', 'A', 'J'}, 1};

namespace record_field {
constexpr std::uint32_t kKind = 1;

// SessionStart
constexpr std::uint32_t kUuid = 2;
constexpr std::uint32_t kDestination = 3;
constexpr std::uint32_t kHostName = 4;
constexpr std::uint32_t kUserName = 5;
constexpr std::uint32_t kOptionsDigest = 6;
constexpr std::uint32_t kSourceDigest = 7;

// BlockWritten
constexpr std::uint32_t kBlockId = 8;
constexpr std::uint32_t kLogicalEnd = 9;
constexpr std::uint32_t kRawSize = 10;
constexpr std::uint32_t kRawHash = 11;
constexpr std::uint32_t kStreamOffset = 13;
constexpr std::uint32_t kStoredSize = 14;
constexpr std::uint32_t kCodec = 15;
constexpr std::uint32_t kEncrypted = 16;

// EntryPlaced
constexpr std::uint32_t kEntry = 12;
}  // namespace record_field

}  // namespace

namespace {

/// Removes entries whose block is not in the list, and reports how many went.
///
/// An entry with no content - a directory, a symbolic link, an empty file -
/// points at no block and is always kept: there is nothing on the drive for it
/// to be missing.
std::size_t dropEntriesWithoutABlock(std::vector<ManifestEntry>& entries,
                                     const std::vector<JournalBlock>& blocks) {
    std::set<std::uint32_t> present;
    for (const JournalBlock& block : blocks) {
        present.insert(block.blockId);
    }

    const std::size_t before = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&present](const ManifestEntry& entry) {
                                     if (entry.location.length == 0) {
                                         return false;
                                     }
                                     return present.count(entry.location.blockId) == 0;
                                 }),
                  entries.end());
    return before - entries.size();
}

/// Keeps the last record of each entry and drops the earlier ones.
///
/// The journal is an append-only log, so the last word about an entry is the
/// truth about it - and a resumed capture really can write about the same file
/// twice. It happens like this: the first run records an entry, the block it
/// went into never reaches the drive, the resume therefore drops that entry
/// and captures the file again, and appends a second record for it. The first
/// record is still in the file, in front of the resume point, and if both were
/// believed the manifest would carry the same path twice - which a restore
/// would then write twice, or refuse.
void keepTheLastWordOnEachEntry(std::vector<ManifestEntry>& entries) {
    std::map<std::pair<PathTokenId, std::string>, std::size_t> latest;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        latest[{entries[i].path.token, entries[i].path.relative}] = i;
    }
    if (latest.size() == entries.size()) {
        return;
    }

    std::vector<ManifestEntry> kept;
    kept.reserve(latest.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (latest[{entries[i].path.token, entries[i].path.relative}] == i) {
            kept.push_back(std::move(entries[i]));
        }
    }
    entries = std::move(kept);
}

}  // namespace

void JournalContents::keepOnlyWhatFitsIn(std::uint64_t archiveLength) {
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                                [archiveLength](const JournalBlock& block) {
                                    return block.logicalEnd > archiveLength;
                                }),
                 blocks.end());
    dropEntriesWithoutABlock(entries, blocks);
}

std::filesystem::path TransferJournal::pathFor(const std::filesystem::path& archive) {
    std::filesystem::path path = archive;
    path += std::string(kSuffix);
    return path;
}

TransferJournal::~TransferJournal() {
    // Ignoring the result: the destructor runs on the failure paths too, and
    // a sync that fails there has nothing left to tell anybody. Callers that
    // need to know call close().
    (void)close();
}

Result<std::unique_ptr<TransferJournal>> TransferJournal::begin(
    const std::filesystem::path& archive, const JournalFingerprint& fingerprint) {
    auto journal = std::unique_ptr<TransferJournal>(new TransferJournal());

    TRANSMIT_TRY(stream, FileStream::open(pathFor(archive), FileStream::Mode::Write));
    journal->stream_ = std::move(stream);

    const auto header = encodeJournalHeader(kFormat);
    TRANSMIT_CHECK(journal->stream_.write(ByteView(header)));
    journal->bytesWritten_ = kJournalHeaderSize;

    ByteBuffer payload;
    ByteWriter writer(payload);
    writer.putUInt(record_field::kKind,
                   static_cast<std::uint64_t>(JournalRecordKind::SessionStart));
    writer.putBytes(record_field::kUuid, ByteView(fingerprint.archiveUuid));
    writer.putString(record_field::kDestination, fingerprint.destination);
    writer.putString(record_field::kHostName, fingerprint.hostName);
    writer.putString(record_field::kUserName, fingerprint.userName);
    writer.putUInt(record_field::kOptionsDigest, fingerprint.optionsDigest);
    writer.putUInt(record_field::kSourceDigest, fingerprint.sourceDigest);

    TRANSMIT_CHECK(journal->append(JournalRecordKind::SessionStart, payload));

    // Synced before the archive is opened, so there is never a block on the
    // drive that no journal describes.
    TRANSMIT_CHECK(journal->sync());
    return journal;
}

Result<std::unique_ptr<TransferJournal>> TransferJournal::reopen(
    const std::filesystem::path& archive, std::uint64_t keepBytes) {
    const std::filesystem::path path = pathFor(archive);

    std::error_code code;
    const auto existing = std::filesystem::file_size(path, code);
    if (code) {
        return Error{ErrorCode::NotFound, "There is no journal to carry on from."};
    }
    if (keepBytes > existing) {
        return Error{ErrorCode::InvalidArgument,
                     "The journal is shorter than the records read out of it."};
    }

    // A torn tail is cut off before anything is appended. Leaving it would put
    // a record that failed its checksum in front of good ones, and the next
    // read would stop at it and lose everything written after.
    if (keepBytes < existing) {
        std::filesystem::resize_file(path, keepBytes, code);
        if (code) {
            Error error{ErrorCode::IoError, "Could not trim the journal's damaged tail."};
            error.systemCode = code.value();
            return error;
        }
    }

    auto journal = std::unique_ptr<TransferJournal>(new TransferJournal());
    TRANSMIT_TRY(stream, FileStream::open(path, FileStream::Mode::ReadWrite));
    journal->stream_ = std::move(stream);
    TRANSMIT_CHECK(journal->stream_.seek(keepBytes));
    journal->bytesWritten_ = keepBytes;
    return journal;
}

Status TransferJournal::append(JournalRecordKind kind, ByteView payload) {
    (void)kind;  // carried inside the payload; the frame stays fixed-width
    return appendJournalRecord(stream_, payload, bytesWritten_);
}

Status TransferJournal::recordBlock(const JournalBlock& block) {
    ByteBuffer payload;
    ByteWriter writer(payload);
    writer.putUInt(record_field::kKind,
                   static_cast<std::uint64_t>(JournalRecordKind::BlockWritten));
    writer.putUInt(record_field::kBlockId, block.blockId);
    writer.putUInt(record_field::kStreamOffset, block.streamOffset);
    writer.putUInt(record_field::kLogicalEnd, block.logicalEnd);
    writer.putUInt(record_field::kRawSize, block.rawSize);
    writer.putUInt(record_field::kStoredSize, block.storedSize);
    writer.putUInt(record_field::kCodec, static_cast<std::uint64_t>(block.codec));
    writer.putBool(record_field::kEncrypted, block.encrypted);
    writer.putBytes(record_field::kRawHash, ByteView(block.rawHash));
    return append(JournalRecordKind::BlockWritten, payload);
}

Status TransferJournal::recordEntry(const ManifestEntry& entry) {
    ByteBuffer payload;
    ByteWriter writer(payload);
    writer.putUInt(record_field::kKind, static_cast<std::uint64_t>(JournalRecordKind::EntryPlaced));
    // The same encoding the manifest uses, so an entry that survives a resume
    // is byte-for-byte the entry the uninterrupted capture would have written.
    writer.putBytes(record_field::kEntry, encodeManifestEntry(entry));
    return append(JournalRecordKind::EntryPlaced, payload);
}

Status TransferJournal::recordComplete() {
    ByteBuffer payload;
    ByteWriter writer(payload);
    writer.putUInt(record_field::kKind,
                   static_cast<std::uint64_t>(JournalRecordKind::SessionComplete));
    TRANSMIT_CHECK(append(JournalRecordKind::SessionComplete, payload));
    return sync();
}

Status TransferJournal::sync() {
    if (!stream_.isOpen()) {
        return {};
    }
    TRANSMIT_CHECK(stream_.flush());
    return stream_.sync();
}

Status TransferJournal::close() {
    if (!stream_.isOpen()) {
        return {};
    }
    const auto status = sync();
    stream_.close();
    return status;
}

Status TransferJournal::discard(const std::filesystem::path& archive) {
    std::error_code code;
    std::filesystem::remove(pathFor(archive), code);
    if (code) {
        Error error{ErrorCode::IoError, "Could not remove the journal."};
        error.systemCode = code.value();
        return error;
    }
    return {};
}

Result<JournalContents> readTransferJournal(const std::filesystem::path& archive) {
    const std::filesystem::path path = TransferJournal::pathFor(archive);

    std::error_code code;
    if (!std::filesystem::exists(path, code)) {
        return Error{ErrorCode::NotFound, "There is no journal beside that archive."};
    }

    TRANSMIT_TRY(raw, readWholeFile(path));
    const ByteView data(raw);
    TRANSMIT_CHECK(checkJournalHeader(data, kFormat));

    JournalContents contents;
    bool sawStart = false;

    TRANSMIT_TRY(
        scan, scanJournalRecords(data, [&](ByteView payload) -> Status {
            ByteReader reader(payload);
            auto kind = JournalRecordKind::SessionStart;
            JournalBlock block;
            std::optional<ManifestEntry> entry;
            JournalFingerprint fingerprint;
            bool malformed = false;

            while (!reader.atEnd()) {
                auto tag = reader.getTag();
                if (!tag) {
                    malformed = true;
                    break;
                }
                switch (tag->field) {
                    case record_field::kKind: {
                        auto value = reader.getVarint();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        kind = static_cast<JournalRecordKind>(*value);
                        break;
                    }
                    case record_field::kUuid: {
                        auto value = reader.getBytes();
                        if (!value || value->size() != fingerprint.archiveUuid.size()) {
                            malformed = true;
                            break;
                        }
                        std::copy(value->begin(), value->end(), fingerprint.archiveUuid.begin());
                        break;
                    }
                    case record_field::kDestination: {
                        auto value = reader.getString();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        fingerprint.destination = *value;
                        break;
                    }
                    case record_field::kHostName: {
                        auto value = reader.getString();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        fingerprint.hostName = *value;
                        break;
                    }
                    case record_field::kUserName: {
                        auto value = reader.getString();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        fingerprint.userName = *value;
                        break;
                    }
                    case record_field::kOptionsDigest: {
                        auto value = reader.getVarint();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        fingerprint.optionsDigest = *value;
                        break;
                    }
                    case record_field::kSourceDigest: {
                        auto value = reader.getVarint();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        fingerprint.sourceDigest = *value;
                        break;
                    }
                    case record_field::kBlockId: {
                        auto value = reader.getVarint();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        block.blockId = static_cast<std::uint32_t>(*value);
                        break;
                    }
                    case record_field::kLogicalEnd: {
                        auto value = reader.getVarint();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        block.logicalEnd = *value;
                        break;
                    }
                    case record_field::kRawSize: {
                        auto value = reader.getVarint();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        block.rawSize = *value;
                        break;
                    }
                    case record_field::kStreamOffset: {
                        auto value = reader.getVarint();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        block.streamOffset = *value;
                        break;
                    }
                    case record_field::kStoredSize: {
                        auto value = reader.getVarint();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        block.storedSize = *value;
                        break;
                    }
                    case record_field::kCodec: {
                        auto value = reader.getVarint();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        block.codec = static_cast<CodecId>(*value);
                        break;
                    }
                    case record_field::kEncrypted: {
                        auto value = reader.getBool();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        block.encrypted = *value;
                        break;
                    }
                    case record_field::kRawHash: {
                        auto value = reader.getBytes();
                        if (!value || value->size() != block.rawHash.size()) {
                            malformed = true;
                            break;
                        }
                        std::copy(value->begin(), value->end(), block.rawHash.begin());
                        break;
                    }
                    case record_field::kEntry: {
                        auto value = reader.getBytes();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        auto decoded = decodeManifestEntry(*value);
                        if (!decoded) {
                            malformed = true;
                            break;
                        }
                        entry = std::move(decoded).value();
                        break;
                    }
                    default: {
                        if (!reader.skip(tag->type)) {
                            malformed = true;
                        }
                        break;
                    }
                }
                if (malformed) {
                    break;
                }
            }

            if (malformed) {
                // A record whose checksum matched but whose contents do not parse
                // is not a torn tail - it is a journal that says something this
                // version cannot read, and continuing past it would silently drop
                // whatever it claimed.
                return Error{ErrorCode::CorruptArchive,
                             "A journal record checksummed correctly and could not be read."};
            }

            switch (kind) {
                case JournalRecordKind::SessionStart:
                    if (sawStart) {
                        return Error{ErrorCode::CorruptArchive,
                                     "The journal holds more than one capture."};
                    }
                    sawStart = true;
                    contents.fingerprint = std::move(fingerprint);
                    break;
                case JournalRecordKind::BlockWritten:
                    contents.blocks.push_back(block);
                    break;
                case JournalRecordKind::EntryPlaced:
                    if (!entry) {
                        return Error{ErrorCode::CorruptArchive,
                                     "A journal entry record held no entry."};
                    }
                    contents.entries.push_back(std::move(*entry));
                    break;
                case JournalRecordKind::SessionComplete:
                    contents.complete = true;
                    break;
            }
            return {};
        }));

    contents.tailDiscarded = scan.tailDiscarded;
    contents.validBytes = scan.validBytes;

    if (!sawStart) {
        return Error{ErrorCode::CorruptArchive, "The journal never says which capture it is for."};
    }

    keepTheLastWordOnEachEntry(contents.entries);
    dropEntriesWithoutABlock(contents.entries, contents.blocks);
    return contents;
}

std::uint64_t journalDigest(ByteView data) {
    const Digest256 digest = Blake2b::hash256(data);
    return readLe<std::uint64_t>(ByteView(digest).subspan(0, 8));
}

}  // namespace transmit::format
