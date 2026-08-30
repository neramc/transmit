#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace transmit::test_support {

/// A scratch directory no other test can be using.
///
/// Naming one after a per-process counter is not enough, and the way it fails
/// is worth writing down: ctest runs each test case as its own process, and
/// every one of those processes starts its counter at zero. Two cases of the
/// same fixture running side by side therefore choose the same directory, and
/// whichever reaches `SetUp` second deletes the files the first is in the
/// middle of using. It looks exactly like a flaky test - the assertion that
/// fails is a real one, about a file that really is empty - and it only
/// happens when the suite is run in parallel, which is to say on the build
/// machine and not on the one where somebody is trying to reproduce it.
///
/// So the name carries the process id as well: unique across the processes
/// ctest spawns, and unique within one of them through the counter.
inline std::filesystem::path makeTemporaryDirectory(std::string_view prefix) {
#if defined(_WIN32)
    const auto process = static_cast<long long>(::_getpid());
#else
    const auto process = static_cast<long long>(::getpid());
#endif

    static int counter = 0;
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        (std::string(prefix) + "-" + std::to_string(process) + "-" + std::to_string(counter++));

    std::error_code code;
    std::filesystem::remove_all(directory, code);
    std::filesystem::create_directories(directory);
    return directory;
}

/// Removes a directory made by makeTemporaryDirectory. Failure is ignored:
/// a test that has already reported its result has nothing useful to say
/// about the weather in /tmp.
inline void removeTemporaryDirectory(const std::filesystem::path& directory) {
    std::error_code code;
    std::filesystem::remove_all(directory, code);
}

}  // namespace transmit::test_support
