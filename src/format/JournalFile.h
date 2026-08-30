#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "format/Bytes.h"
#include "format/FileIo.h"
#include "format/Result.h"

namespace transmit::format {

/// The frame both of Transmit's journals are written in.
///
/// A journal exists to be read after the run that wrote it was interrupted, so
/// its own last record is the one part of it that cannot be trusted. The frame
/// is built around that: every record carries its length and a CRC-32 of its
/// payload ahead of the payload itself, so a tail cut anywhere - in the length
/// field, inside the payload, between the two - is caught either by running out
/// of bytes or by the checksum, and everything written before it still stands.
///
/// The two journals share this file rather than keeping a copy each, because
/// "what a torn tail looks like" is the one piece of this program where a
/// second opinion costs somebody their data.
struct JournalFormat {
    /// Four characters saying which journal this is. A capture journal and a
    /// restore journal are the same shape and mean entirely different things,
    /// so reading one as the other has to fail at the first byte.
    std::array<char, 4> magic;
    std::uint16_t version;
};

constexpr std::size_t kJournalHeaderSize = 16;

/// Length, then checksum. The length comes first so a tail cut in the middle
/// of a record is caught by the reader running out of bytes, which is cheaper
/// than a checksum and catches the case where the checksum itself was torn.
constexpr std::size_t kJournalRecordHeaderSize = 8;

/// A record is a path and some numbers. Anything larger is a corrupt length
/// field being believed - and believing it means allocating whatever it says.
constexpr std::uint32_t kMaxJournalRecordSize = 1u << 20;

[[nodiscard]] std::array<Byte, kJournalHeaderSize> encodeJournalHeader(const JournalFormat& format);

/// Refuses a file that is not this journal, and one written by a newer
/// Transmit. A journal half-understood is worse than none: what it says about
/// what reached the drive is acted on as exactly true.
[[nodiscard]] Status checkJournalHeader(ByteView data, const JournalFormat& format);

/// Appends one framed record and adds its framed size to `bytesWritten`.
[[nodiscard]] Status appendJournalRecord(FileStream& stream, ByteView payload,
                                         std::uint64_t& bytesWritten);

/// How far a scan got.
struct JournalScan {
    /// Records at the end were incomplete or failed their checksum and were
    /// discarded. Expected after a power cut - the journal is written in front
    /// of the thing it describes, so its own tail is what gets torn - and not
    /// an error.
    bool tailDiscarded = false;

    /// The header plus the records that survived, which is what the journal is
    /// truncated to before it is appended to again.
    std::uint64_t validBytes = kJournalHeaderSize;
};

/// Walks the records after the header, handing each payload to `onRecord`.
///
/// Stops without complaint at the first torn record: that is what an
/// interrupted append leaves behind, and the records in front of it are the
/// answer being asked for. Stops with whatever error `onRecord` returned if a
/// record that checksummed correctly could not be understood - a different
/// thing entirely, because the journal is then saying something this version
/// cannot read, and carrying on past it would silently drop whatever it
/// claimed had reached the drive.
[[nodiscard]] Result<JournalScan> scanJournalRecords(
    ByteView data, const std::function<Status(ByteView payload)>& onRecord);

}  // namespace transmit::format
