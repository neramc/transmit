#pragma once

#include "format/Bytes.h"
#include "format/hash/Blake2b.h"
#include "format/hash/Md5.h"

namespace transmit::format {

/// Both digests of one file's contents.
///
/// They answer different questions and neither replaces the other. `blake2b`
/// is the identity: it decides whether two files are the same, keys the
/// deduplication table and proves a block came back intact. `md5` is there so
/// a person can confirm the drive holds what Transmit wrote using a tool that
/// has never heard of Transmit.
struct ContentDigests {
    Digest256 blake2b{};
    Digest128 md5{};
};

/// Hashes a buffer with both, walking it once.
///
/// "Once" is the point: a 64 MiB block hashed twice end to end is read from
/// main memory twice, and the second pass finds nothing left in cache. Feeding
/// both hashers a chunk at a time keeps the bytes hot between them.
///
/// With `withMd5` false the MD5 is left zeroed and not computed, which is what
/// `ArchiveOptions::recordMd5` being off means.
[[nodiscard]] ContentDigests hashContent(ByteView data, bool withMd5 = true);

}  // namespace transmit::format
