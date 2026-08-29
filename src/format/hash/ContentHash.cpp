#include "format/hash/ContentHash.h"

namespace transmit::format {
namespace {

/// Big enough that the per-chunk overhead disappears, small enough that a
/// chunk is still in the level-two cache when the second hasher reads it.
constexpr std::size_t kChunkSize = 256 * 1024;

}  // namespace

ContentDigests hashContent(ByteView data, bool withMd5) {
    ContentDigests digests;

    if (!withMd5) {
        digests.blake2b = Blake2b::hash256(data);
        return digests;
    }

    Blake2b blake(32);
    Md5 md5;
    for (std::size_t offset = 0; offset < data.size(); offset += kChunkSize) {
        const std::size_t take =
            data.size() - offset < kChunkSize ? data.size() - offset : kChunkSize;
        const ByteView chunk = data.subspan(offset, take);
        blake.update(chunk);
        md5.update(chunk);
    }

    digests.blake2b = blake.finish256();
    digests.md5 = md5.finish128();
    return digests;
}

}  // namespace transmit::format
