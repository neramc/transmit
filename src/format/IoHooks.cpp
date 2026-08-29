#include "format/IoHooks.h"

#include <atomic>

namespace transmit::format {
namespace {

// A plain global rather than thread_local: the capture pipeline hashes and
// compresses on worker threads and writes on another, so hooks installed by a
// test have to be visible from all of them. Relaxed is enough because the only
// writer is a test that is not running anything else at the time.
std::atomic<const IoHooks*> gHooks{nullptr};

}  // namespace

void setIoHooks(const IoHooks* hooks) noexcept {
    gHooks.store(hooks, std::memory_order_release);
}

const IoHooks* ioHooks() noexcept {
    return gHooks.load(std::memory_order_acquire);
}

}  // namespace transmit::format
