#pragma once

#include <QFuture>
#include <QThreadPool>

#include <deque>
#include <functional>
#include <memory>
#include <utility>

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

    /// Called after a block's bytes have reached the writer, with the record
    /// the manifest will carry for it and how long the stream is now.
    ///
    /// This is the moment - and the only one - at which a block can honestly
    /// be written down as being on the drive. Compression runs on several
    /// threads and a block's id is handed out when it is queued, so anywhere
    /// earlier would record a block the archive has not got. Runs on the
    /// calling thread, never on a worker. A failure here fails the capture:
    /// a journal that has fallen behind the archive is worse than none.
    using BlockWritten =
        std::function<format::Status(const format::BlockRecord& record, quint64 logicalEnd)>;
    void setBlockWritten(BlockWritten callback) { blockWritten_ = std::move(callback); }

    /// Lets a run stop part way through compressing a block as well as between
    /// blocks. Read from the worker threads, so it must be safe to call from
    /// several at once; set it before the first submit().
    void setAbortCheck(format::AbortCheck abort) { abort_ = std::move(abort); }
    ~BlockPipeline();

    BlockPipeline(const BlockPipeline&) = delete;
    BlockPipeline& operator=(const BlockPipeline&) = delete;

    /// Copies `raw`, schedules its compression and returns the block id it will
    /// be written under. Blocks when too many are already in flight.
    format::Result<quint32> submit(format::ByteView raw);

    /// Waits for every outstanding block and writes it. Call before finishing
    /// the archive.
    ///
    /// `isCancelled`, when given, is asked between blocks. A capture spends
    /// nearly all of its time here, so a stop that only took effect once the
    /// backlog had been compressed and written would look, to the person who
    /// asked for it, like no stop at all. Blocks already in a worker still
    /// finish - a codec call cannot be interrupted part way - but nothing
    /// further is started, and nothing more is written.
    format::Status drain(const std::function<bool()>& isCancelled = {});

    [[nodiscard]] int workerCount() const noexcept { return workers_; }

private:
    struct Pending {
        quint32 blockId = 0;
        QFuture<format::Result<format::PreparedBlock>> future;
    };

    format::Status writeFront();

    format::ArchiveWriter& writer_;
    format::AbortCheck abort_;
    BlockWritten blockWritten_;
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
