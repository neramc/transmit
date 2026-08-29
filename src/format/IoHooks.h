#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

#include "format/Result.h"

namespace transmit::format {

/// Interception points for the file layer, so a test can arrange the failures
/// that matter most and are hardest to produce on demand: a stick that fills up
/// half way through a write, one that is pulled out, one that accepts a write
/// and stores something shorter, one that reads back a different byte than it
/// was given.
///
/// Every hook is empty by default and checked with one relaxed atomic load per
/// operation - operations that are already a system call over kilobytes at a
/// time - so an ordinary build pays nothing that can be measured. They are in
/// the shipping binary rather than behind a compile flag on purpose: a fault
/// path that only exists in a special build is a fault path nobody runs.
struct IoHooks {
    /// Consulted before each write. Returning an error fails the write as
    /// though the system had, with whatever code is wanted - ENOSPC to fill the
    /// disk, ENODEV to pull the stick out.
    std::function<std::optional<Error>(const std::filesystem::path&, std::uint64_t offset,
                                       std::size_t size)>
        beforeWrite;

    /// How many of the offered bytes are allowed to land. Returning less than
    /// `size` produces a short write, which is what a device that is nearly
    /// full does before it starts refusing outright.
    std::function<std::size_t(const std::filesystem::path&, std::size_t size)> writeLimit;

    /// Consulted before each read, the same way.
    std::function<std::optional<Error>(const std::filesystem::path&, std::uint64_t offset,
                                       std::size_t size)>
        beforeRead;
};

/// Installs hooks for every thread. Test-only: nothing in the application ever
/// calls it, and calling it while a capture is running is not supported.
void setIoHooks(const IoHooks* hooks) noexcept;

/// The hooks in force, or nullptr.
[[nodiscard]] const IoHooks* ioHooks() noexcept;

/// Installs hooks for a scope and takes them away again, including when the
/// test that installed them fails part way through.
class ScopedIoHooks {
public:
    explicit ScopedIoHooks(IoHooks hooks) : hooks_(std::move(hooks)) { setIoHooks(&hooks_); }
    ~ScopedIoHooks() { setIoHooks(nullptr); }

    ScopedIoHooks(const ScopedIoHooks&) = delete;
    ScopedIoHooks& operator=(const ScopedIoHooks&) = delete;

private:
    IoHooks hooks_;
};

}  // namespace transmit::format
