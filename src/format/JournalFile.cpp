#include "format/JournalFile.h"

#include "format/Serialization.h"
#include "format/hash/Crc32.h"

namespace transmit::format {

std::array<Byte, kJournalHeaderSize> encodeJournalHeader(const JournalFormat& format) {
    std::array<Byte, kJournalHeaderSize> out{};
    for (std::size_t i = 0; i < format.magic.size(); ++i) {
        out[i] = static_cast<Byte>(format.magic[i]);
    }
    writeLe<std::uint16_t>(MutableByteView(out).subspan(4, 2), format.version);
    return out;
}

Status checkJournalHeader(ByteView data, const JournalFormat& format) {
    if (data.size() < kJournalHeaderSize) {
        return Error{ErrorCode::CorruptArchive, "The journal is too short to have a header."};
    }
    for (std::size_t i = 0; i < format.magic.size(); ++i) {
        if (data[i] != static_cast<Byte>(format.magic[i])) {
            return Error{ErrorCode::CorruptArchive, "That file is not a Transmit journal."};
        }
    }
    if (readLe<std::uint16_t>(data.subspan(4, 2)) != format.version) {
        return Error{ErrorCode::UnsupportedVersion,
                     "The journal was written by a newer version of Transmit."};
    }
    return {};
}

Status appendJournalRecord(FileStream& stream, ByteView payload, std::uint64_t& bytesWritten) {
    if (payload.size() > kMaxJournalRecordSize) {
        return Error{ErrorCode::Internal, "A journal record grew past its limit."};
    }

    std::array<Byte, kJournalRecordHeaderSize> header{};
    writeLe<std::uint32_t>(MutableByteView(header).subspan(0, 4),
                           static_cast<std::uint32_t>(payload.size()));
    writeLe<std::uint32_t>(MutableByteView(header).subspan(4, 4), crc32(payload));

    TRANSMIT_CHECK(stream.write(ByteView(header)));
    TRANSMIT_CHECK(stream.write(payload));
    bytesWritten += kJournalRecordHeaderSize + payload.size();
    return {};
}

Result<JournalScan> scanJournalRecords(ByteView data,
                                       const std::function<Status(ByteView)>& onRecord) {
    JournalScan scan;
    std::size_t offset = kJournalHeaderSize;

    while (offset < data.size()) {
        // Any of these three is a tail that was being written when the run
        // stopped. None of them is corruption in the sense that anybody needs
        // to be told about it - it is what an interrupted append looks like.
        if (data.size() - offset < kJournalRecordHeaderSize) {
            scan.tailDiscarded = true;
            break;
        }
        const auto length = readLe<std::uint32_t>(data.subspan(offset, 4));
        const auto checksum = readLe<std::uint32_t>(data.subspan(offset + 4, 4));
        if (length > kMaxJournalRecordSize ||
            data.size() - offset - kJournalRecordHeaderSize < length) {
            scan.tailDiscarded = true;
            break;
        }

        const ByteView payload = data.subspan(offset + kJournalRecordHeaderSize, length);
        if (crc32(payload) != checksum) {
            scan.tailDiscarded = true;
            break;
        }

        TRANSMIT_CHECK(onRecord(payload));

        offset += kJournalRecordHeaderSize + length;
        scan.validBytes = offset;
    }

    return scan;
}

}  // namespace transmit::format
