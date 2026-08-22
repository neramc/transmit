#include "format/BlockPacker.h"

#include <utility>

namespace transmit::format {

BlockPacker::BlockPacker(std::uint64_t blockSize, BlockSink sink)
    : blockSize_(blockSize == 0 ? 1 : blockSize), sink_(std::move(sink)) {}

Result<BlockPacker::PlacementId> BlockPacker::add(ByteView content) {
    return add(Blake2b::hash256(content), content);
}

Result<BlockPacker::PlacementId> BlockPacker::add(const Digest256& hash, ByteView content) {
    const PlacementId id = placements_.size();

    if (content.empty()) {
        placements_.push_back(Placement{BlockLocation{0, 0, 0}, true, false});
        return id;
    }

    if (const auto it = byHash_.find(hash); it != byHash_.end()) {
        deduplicatedBytes_ += content.size();
        placements_.push_back(Placement{it->second, true, true});
        return id;
    }

    // The same content may already be sitting in the block currently being
    // filled, whose id is not known yet; alias it and resolve at flush.
    if (const auto it = pendingByHash_.find(hash); it != pendingByHash_.end()) {
        deduplicatedBytes_ += content.size();
        const Placement& original = placements_[it->second];
        placements_.push_back(Placement{original.location, false, true});
        pendingAliases_.emplace_back(id, it->second);
        return id;
    }

    if (content.size() >= blockSize_) {
        TRANSMIT_CHECK(flush());
        TRANSMIT_TRY(blockId, sink_(content));
        ++blockCount_;

        const BlockLocation location{blockId, 0, content.size()};
        byHash_.emplace(hash, location);
        placements_.push_back(Placement{location, true, false});
        return id;
    }

    if (pending_.size() + content.size() > blockSize_) {
        TRANSMIT_CHECK(flush());
    }

    const auto offset = static_cast<std::uint64_t>(pending_.size());
    pending_.insert(pending_.end(), content.begin(), content.end());

    placements_.push_back(Placement{BlockLocation{0, offset, content.size()}, false, false});
    pendingIds_.emplace_back(id, hash);
    pendingByHash_.emplace(hash, id);
    return id;
}

Status BlockPacker::flush() {
    if (pending_.empty()) {
        return ok();
    }

    TRANSMIT_TRY(blockId, sink_(pending_));
    ++blockCount_;

    for (const auto& [id, hash] : pendingIds_) {
        Placement& placement = placements_[id];
        placement.location.blockId = blockId;
        placement.resolved = true;
        byHash_.emplace(hash, placement.location);
    }

    for (const auto& [duplicate, original] : pendingAliases_) {
        placements_[duplicate].location = placements_[original].location;
        placements_[duplicate].resolved = true;
    }

    pending_.clear();
    pendingIds_.clear();
    pendingByHash_.clear();
    pendingAliases_.clear();
    return ok();
}

Result<BlockLocation> BlockPacker::location(PlacementId id) const {
    if (id >= placements_.size()) {
        return makeError(ErrorCode::InvalidArgument, "unknown placement handle");
    }
    const Placement& placement = placements_[id];
    if (!placement.resolved) {
        return makeError(ErrorCode::Internal,
                         "this placement is still buffered: flush the packer first");
    }
    return placement.location;
}

bool BlockPacker::isDeduplicated(PlacementId id) const {
    return id < placements_.size() && placements_[id].deduplicated;
}

}  // namespace transmit::format
