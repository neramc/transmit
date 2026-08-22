#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "format/Bytes.h"
#include "format/Manifest.h"
#include "format/Result.h"
#include "format/hash/Blake2b.h"

namespace transmit::format {

/// Packs many small files into few large "solid" blocks.
///
/// Compressing each file on its own throws away the redundancy between them,
/// and an environment capture is mostly small, highly similar files (config
/// files, icon sets, browser profiles). Concatenating them into one block
/// before compression is what turns a large capture into a small archive.
///
/// The packer also deduplicates: identical content is stored once and every
/// entry that shares it points at the same slice.
///
/// Because a block's id is only known when the block is written, add() hands
/// back a stable handle instead of a location. The caller resolves handles to
/// locations after flush(), which is why the capture pipeline keeps its
/// manifest entries until the archive is finished.
class BlockPacker {
public:
    /// Hands a completed block to the writer and returns the assigned id.
    using BlockSink = std::function<Result<std::uint32_t>(ByteView raw)>;

    using PlacementId = std::size_t;

    BlockPacker(std::uint64_t blockSize, BlockSink sink);

    /// Adds one file's content. A file at least as large as the block size is
    /// given a block of its own rather than sharing one, so a partial restore
    /// never has to decompress far more than it asked for.
    Result<PlacementId> add(ByteView content);

    /// Same as add() when the caller already hashed the content while reading
    /// it, which the capture pipeline does to avoid a second pass.
    Result<PlacementId> add(const Digest256& hash, ByteView content);

    /// Writes whatever is buffered and finalises the locations of everything
    /// added since the previous flush. Must be called before the archive is
    /// finished, otherwise the last block is lost.
    Status flush();

    /// Valid once the block holding this placement has been flushed.
    [[nodiscard]] Result<BlockLocation> location(PlacementId id) const;

    [[nodiscard]] bool isDeduplicated(PlacementId id) const;

    [[nodiscard]] std::uint64_t deduplicatedBytes() const noexcept { return deduplicatedBytes_; }
    [[nodiscard]] std::uint64_t pendingBytes() const noexcept { return pending_.size(); }
    [[nodiscard]] std::size_t blockCount() const noexcept { return blockCount_; }

private:
    struct Placement {
        BlockLocation location;
        bool resolved = false;
        bool deduplicated = false;
    };

    std::uint64_t blockSize_;
    BlockSink sink_;
    ByteBuffer pending_;

    std::vector<Placement> placements_;
    /// Handles waiting for the current block's id, with the hash to register.
    std::vector<std::pair<PlacementId, Digest256>> pendingIds_;
    /// Content already buffered in the block being filled. Duplicates that
    /// arrive before the flush resolve against these rather than being stored
    /// a second time - which is the common case, because identical files tend
    /// to sit next to each other in a scan.
    std::map<Digest256, PlacementId> pendingByHash_;
    /// (duplicate, original) pairs whose location is copied across at flush.
    std::vector<std::pair<PlacementId, PlacementId>> pendingAliases_;

    std::map<Digest256, BlockLocation> byHash_;
    std::uint64_t deduplicatedBytes_ = 0;
    std::size_t blockCount_ = 0;
};

}  // namespace transmit::format
