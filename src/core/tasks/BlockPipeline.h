#pragma once

#include <QFuture>
#include <QThreadPool>
#include <deque>
#include <memory>

#include "format/Container.h"
#include "format/Result.h"

namespace transmit::core {

/// Compresses blocks on a pool of worker threads while a single thread writes
/// them out in order.
///
/// Compression is where a capture spends nearly all its time, and it is
/// embarrassingly parallel across blocks. Writing, by contrast, must stay
/// sequential because block offsets are recorded as the stream is built. This
/// class is the join between the two: submit() returns a block id straight
/// away and does the expensive work in the background, while completed blocks
/// are drained to the writer in submission order.
///
/// The number of blocks in flight is capped, because a block holds its
/// uncompressed bytes plus the codec's working memory, and the Maximum preset's
/// window alone is 128 MiB per worker.
class BlockPipeline {
public:
    /// `workers` of 0 picks a thread count from the machine and the preset.
    BlockPipeline(format::ArchiveWriter& writer, int workers = 0, int maxInFlight = 0);
    ~BlockPipeline();

    BlockPipeline(const BlockPipeline&) = delete;
    BlockPipeline& operator=(const BlockPipeline&) = delete;

    /// Copies `raw`, schedules its compression and returns the block id it will
    /// be written under. Blocks when too many are already in flight.
    format::Result<quint32> submit(format::ByteView raw);

    /// Waits for every outstanding block and writes it. Call before finishing
    /// the archive.
    format::Status drain();

    [[nodiscard]] int workerCount() const noexcept { return workers_; }

private:
    struct Pending {
        quint32 blockId = 0;
        QFuture<format::Result<format::PreparedBlock>> future;
    };

    format::Status writeFront();

    format::ArchiveWriter& writer_;
    std::unique_ptr<QThreadPool> pool_;
    std::deque<Pending> pending_;
    int workers_ = 1;
    int maxInFlight_ = 2;
};

/// How many compression workers to use for a preset. The high presets need a
/// large window per worker, so the count is capped to keep memory sane on a
/// laptop.
int recommendedWorkerCount(format::CompressionPreset preset);

}  // namespace transmit::core
