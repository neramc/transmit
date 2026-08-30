#include "format/RestoreJournal.h"

#include <algorithm>
#include <map>
#include <utility>

#include "format/Serialization.h"

namespace transmit::format {
namespace {

/// "TXAR": the restore journal, as against the capture journal's "TXAJ". The
/// two are the same shape and mean opposite things, so reading one as the
/// other has to fail at the first byte rather than at the first record.
constexpr JournalFormat kFormat{{'T', 'X', 'A', 'R'}, 1};

namespace record_field {
constexpr std::uint32_t kKind = 1;

// SessionStart
constexpr std::uint32_t kUuid = 2;
constexpr std::uint32_t kDestination = 3;
constexpr std::uint32_t kHostName = 4;
constexpr std::uint32_t kUserName = 5;
constexpr std::uint32_t kOptionsDigest = 6;
constexpr std::uint32_t kRollbackPath = 7;

// ItemPlaced
constexpr std::uint32_t kSource = 8;
constexpr std::uint32_t kTarget = 9;
constexpr std::uint32_t kOutcome = 10;
}  // namespace record_field

/// The last record about an item is the one that counts.
///
/// A resumed restore genuinely settles the same item twice: the first run put
/// it down and was interrupted before it could say so, or said so and then the
/// second run redid it. Both records are true accounts of what happened; only
/// the later one is a true account of what is on disk now.
void keepTheLastWordOnEachItem(std::vector<RestorePlacement>& placements) {
    std::map<std::string, std::size_t> latest;
    for (std::size_t i = 0; i < placements.size(); ++i) {
        latest[placements[i].source] = i;
    }
    if (latest.size() == placements.size()) {
        return;
    }

    std::vector<std::size_t> keep;
    keep.reserve(latest.size());
    for (const auto& [source, index] : latest) {
        keep.push_back(index);
    }
    std::sort(keep.begin(), keep.end());

    std::vector<RestorePlacement> kept;
    kept.reserve(keep.size());
    for (const std::size_t index : keep) {
        kept.push_back(std::move(placements[index]));
    }
    placements = std::move(kept);
}

}  // namespace

std::uint64_t RestoreJournalContents::writtenCount() const noexcept {
    return static_cast<std::uint64_t>(
        std::count_if(placements.begin(), placements.end(), [](const RestorePlacement& placement) {
            return placement.outcome == RestoreOutcome::Written;
        }));
}

std::filesystem::path RestoreJournal::pathFor(const std::filesystem::path& stateDirectory,
                                              const ArchiveUuid& archive) {
    std::string name = "restore-";
    name.reserve(name.size() + archive.size() * 2 + kSuffix.size());
    for (const Byte byte : archive) {
        static constexpr char kHex[] = "0123456789abcdef";
        name.push_back(kHex[(static_cast<unsigned>(byte) >> 4) & 0xFu]);
        name.push_back(kHex[static_cast<unsigned>(byte) & 0xFu]);
    }
    name.append(kSuffix);
    return stateDirectory / name;
}

RestoreJournal::~RestoreJournal() {
    if (stream_.isOpen()) {
        (void)close();
    }
}

Result<std::unique_ptr<RestoreJournal>> RestoreJournal::begin(
    const std::filesystem::path& path, const RestoreFingerprint& fingerprint) {
    auto journal = std::unique_ptr<RestoreJournal>(new RestoreJournal());

    TRANSMIT_TRY(stream, FileStream::open(path, FileStream::Mode::Write));
    journal->stream_ = std::move(stream);

    const auto header = encodeJournalHeader(kFormat);
    TRANSMIT_CHECK(journal->stream_.write(ByteView(header)));
    journal->bytesWritten_ = kJournalHeaderSize;

    ByteBuffer payload;
    ByteWriter writer(payload);
    writer.putUInt(record_field::kKind,
                   static_cast<std::uint64_t>(RestoreRecordKind::SessionStart));
    writer.putBytes(record_field::kUuid, ByteView(fingerprint.archiveUuid));
    writer.putString(record_field::kDestination, fingerprint.destination);
    writer.putString(record_field::kHostName, fingerprint.hostName);
    writer.putString(record_field::kUserName, fingerprint.userName);
    writer.putUInt(record_field::kOptionsDigest, fingerprint.optionsDigest);
    writer.putString(record_field::kRollbackPath, fingerprint.rollbackArchivePath);

    TRANSMIT_CHECK(appendJournalRecord(journal->stream_, payload, journal->bytesWritten_));
    TRANSMIT_CHECK(journal->sync());
    return journal;
}

Result<std::unique_ptr<RestoreJournal>> RestoreJournal::reopen(const std::filesystem::path& path,
                                                               std::uint64_t keepBytes) {
    std::error_code code;
    const auto existing = std::filesystem::file_size(path, code);
    if (code) {
        return Error{ErrorCode::NotFound, "There is no restore journal to carry on from."};
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

    auto journal = std::unique_ptr<RestoreJournal>(new RestoreJournal());
    TRANSMIT_TRY(stream, FileStream::open(path, FileStream::Mode::ReadWrite));
    journal->stream_ = std::move(stream);
    TRANSMIT_CHECK(journal->stream_.seek(keepBytes));
    journal->bytesWritten_ = keepBytes;
    return journal;
}

Status RestoreJournal::recordPlacement(const RestorePlacement& placement) {
    ByteBuffer payload;
    ByteWriter writer(payload);
    writer.putUInt(record_field::kKind, static_cast<std::uint64_t>(RestoreRecordKind::ItemPlaced));
    writer.putString(record_field::kSource, placement.source);
    writer.putString(record_field::kTarget, placement.target);
    writer.putUInt(record_field::kOutcome, static_cast<std::uint64_t>(placement.outcome));
    return appendJournalRecord(stream_, payload, bytesWritten_);
}

Status RestoreJournal::recordComplete() {
    ByteBuffer payload;
    ByteWriter writer(payload);
    writer.putUInt(record_field::kKind,
                   static_cast<std::uint64_t>(RestoreRecordKind::SessionComplete));
    TRANSMIT_CHECK(appendJournalRecord(stream_, payload, bytesWritten_));
    return sync();
}

Status RestoreJournal::sync() {
    if (!stream_.isOpen()) {
        return {};
    }
    TRANSMIT_CHECK(stream_.flush());
    return stream_.sync();
}

Status RestoreJournal::close() {
    if (!stream_.isOpen()) {
        return {};
    }
    const auto status = sync();
    stream_.close();
    return status;
}

Status RestoreJournal::discard(const std::filesystem::path& path) {
    std::error_code code;
    std::filesystem::remove(path, code);
    if (code) {
        Error error{ErrorCode::IoError, "Could not remove the restore journal."};
        error.systemCode = code.value();
        return error;
    }
    return {};
}

Result<RestoreJournalContents> readRestoreJournal(const std::filesystem::path& path) {
    std::error_code code;
    if (!std::filesystem::exists(path, code)) {
        return Error{ErrorCode::NotFound, "There is no record of an interrupted restore."};
    }

    TRANSMIT_TRY(raw, readWholeFile(path));
    const ByteView data(raw);
    TRANSMIT_CHECK(checkJournalHeader(data, kFormat));

    RestoreJournalContents contents;
    bool sawStart = false;

    TRANSMIT_TRY(
        scan, scanJournalRecords(data, [&](ByteView payload) -> Status {
            ByteReader reader(payload);
            auto kind = RestoreRecordKind::SessionStart;
            RestoreFingerprint fingerprint;
            RestorePlacement placement;
            bool sawOutcome = false;
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
                        kind = static_cast<RestoreRecordKind>(*value);
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
                    case record_field::kRollbackPath: {
                        auto value = reader.getString();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        fingerprint.rollbackArchivePath = *value;
                        break;
                    }
                    case record_field::kSource: {
                        auto value = reader.getString();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        placement.source = *value;
                        break;
                    }
                    case record_field::kTarget: {
                        auto value = reader.getString();
                        if (!value) {
                            malformed = true;
                            break;
                        }
                        placement.target = *value;
                        break;
                    }
                    case record_field::kOutcome: {
                        auto value = reader.getVarint();
                        if (!value ||
                            *value < static_cast<std::uint64_t>(RestoreOutcome::Written) ||
                            *value > static_cast<std::uint64_t>(RestoreOutcome::Failed)) {
                            malformed = true;
                            break;
                        }
                        placement.outcome = static_cast<RestoreOutcome>(*value);
                        sawOutcome = true;
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
                // is not a torn tail - it is a journal saying something this
                // version cannot read, and carrying on past it would silently
                // drop whatever it claimed.
                return Error{
                    ErrorCode::CorruptArchive,
                    "A restore journal record checksummed correctly and could not be read."};
            }

            switch (kind) {
                case RestoreRecordKind::SessionStart:
                    if (sawStart) {
                        return Error{ErrorCode::CorruptArchive,
                                     "The restore journal holds more than one restore."};
                    }
                    sawStart = true;
                    contents.fingerprint = std::move(fingerprint);
                    break;
                case RestoreRecordKind::ItemPlaced:
                    // An item with no source cannot be matched to anything a
                    // resumed run is holding, so it is not a record of anything.
                    if (placement.source.empty() || !sawOutcome) {
                        return Error{ErrorCode::CorruptArchive,
                                     "A restore journal record does not say what it is about."};
                    }
                    contents.placements.push_back(std::move(placement));
                    break;
                case RestoreRecordKind::SessionComplete:
                    contents.complete = true;
                    break;
            }
            return {};
        }));

    contents.tailDiscarded = scan.tailDiscarded;
    contents.validBytes = scan.validBytes;

    if (!sawStart) {
        return Error{ErrorCode::CorruptArchive,
                     "The restore journal never says which restore it is for."};
    }

    keepTheLastWordOnEachItem(contents.placements);
    return contents;
}

}  // namespace transmit::format
