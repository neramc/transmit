#include "core/tasks/BlockPipeline.h"

#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

#include "core/utils/Logging.h"

namespace transmit::core {
namespace {

/// Rough working-set cost of one compression context, used to cap the worker
/// count. zstd level 22 with a 128 MiB window is the expensive case.
quint64 workerMemoryCost(format::CompressionPreset preset) {
    switch (preset) {
        case format::CompressionPreset::Fast:
            return 32ULL * 1024 * 1024;
        case format::CompressionPreset::Balanced:
            return 96ULL * 1024 * 1024;
        case format::CompressionPreset::Maximum:
            return 400ULL * 1024 * 1024;
        case format::CompressionPreset::Extreme:
            return 700ULL * 1024 * 1024;
    }
    return 128ULL * 1024 * 1024;
}

}  // namespace

int recommendedWorkerCount(format::CompressionPreset preset) {
    const int cores = std::max(1, QThread::idealThreadCount());

    // Leave a core for the reader and the UI, then clamp by the memory each
    // worker needs. A budget of 2 GiB keeps Transmit well-behaved next to the
    // applications the user has not closed.
    const int byCores = std::max(1, cores - 1);
    constexpr quint64 kMemoryBudget = 2048ULL * 1024 * 1024;
    const auto byMemory =
        static_cast<int>(std::max<quint64>(1, kMemoryBudget / workerMemoryCost(preset)));

    return std::max(1, std::min(byCores, byMemory));
}

BlockPipeline::BlockPipeline(format::ArchiveWriter& writer, int workers, int maxInFlight)
    : writer_(writer) {
    workers_ = workers > 0 ? workers : recommendedWorkerCount(writer.options().preset);
    // One spare slot keeps every worker fed while the writer drains.
    maxInFlight_ = maxInFlight > 0 ? maxInFlight : workers_ + 1;

    pool_ = std::make_unique<QThreadPool>();
    pool_->setMaxThreadCount(workers_);
    pool_->setObjectName(QStringLiteral("transmit-compress"));

    qCInfo(logCapture) << "compressing with" << workers_ << "workers, up to" << maxInFlight_
                       << "blocks in flight";
}

BlockPipeline::~BlockPipeline() {
    // Never leave worker threads touching the writer after it is gone.
    if (pool_) {
        pool_->waitForDone();
    }
}

format::Status BlockPipeline::writeFront() {
    if (pending_.empty()) {
        return format::ok();
    }

    Pending front = std::move(pending_.front());
    pending_.pop_front();

    front.future.waitForFinished();
    auto prepared = front.future.result();
    if (!prepared) {
        return std::move(prepared).error();
    }
    return writer_.writePrepared(prepared.value());
}

format::Result<quint32> BlockPipeline::submit(format::ByteView raw) {
    while (static_cast<int>(pending_.size()) >= maxInFlight_) {
        TRANSMIT_CHECK(writeFront());
    }

    const quint32 blockId = writer_.nextBlockId();

    // The worker needs its own copy: the caller's buffer is reused for the next
    // block as soon as this call returns.
    auto owned = std::make_shared<format::ByteBuffer>(raw.begin(), raw.end());
    format::ArchiveWriter* writer = &writer_;

    Pending entry;
    entry.blockId = blockId;
    entry.future = QtConcurrent::run(pool_.get(), [writer, blockId, owned, abort = abort_]() {
        return writer->prepare(blockId, *owned, abort);
    });
    pending_.push_back(std::move(entry));
    return blockId;
}

format::Status BlockPipeline::drain(const std::function<bool()>& isCancelled) {
    const auto stop = [this]() {
        // Drop what has not started yet; the destructor waits for the rest.
        // The caller is left with an archive holding fewer blocks than it
        // submitted, which is exactly why it must not go on to finish it.
        pool_->clear();
        qCInfo(logCapture) << "compression stopped with" << pending_.size()
                           << "blocks still outstanding";
        return format::ok();
    };

    while (!pending_.empty()) {
        if (isCancelled && isCancelled()) {
            return stop();
        }
        if (const auto status = writeFront(); !status) {
            // A block whose compression was stopped part way is the same news
            // as the check above, arriving a moment later.
            if (status.error().code == format::ErrorCode::Cancelled) {
                return stop();
            }
            return status.error();
        }
    }
    return format::ok();
}

}  // namespace transmit::core
