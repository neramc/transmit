#pragma once

/// Lets one source file be both a libFuzzer target and an ordinary
/// program that replays a directory of inputs.
///
/// The replay mode is what keeps a crasher fixed. libFuzzer needs clang
/// and a sanitiser runtime, which not every build has; a crasher found
/// once is committed under corpus/regressions/ and replayed by a plain
/// ctest target in the normal build, so the bug cannot come back on a
/// machine that cannot fuzz.

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

#ifndef TRANSMIT_FUZZING_ENABLED

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

/// Replays every file under the directories named on the command line.
inline int replayCorpus(int argc, char** argv) {
    std::size_t replayed = 0;

    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path root(argv[i]);
        if (!std::filesystem::exists(root)) {
            // A regression directory with nothing in it yet is the normal
            // state, not a failure.
            continue;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::ifstream file(entry.path(), std::ios::binary);
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                                  std::istreambuf_iterator<char>());
            std::cout << "replaying " << entry.path().string() << " (" << bytes.size()
                      << " bytes)\n";
            static_cast<void>(LLVMFuzzerTestOneInput(bytes.data(), bytes.size()));
            ++replayed;
        }
    }

    std::cout << "replayed " << replayed << " input(s)\n";
    return 0;
}

int main(int argc, char** argv) {
    return replayCorpus(argc, argv);
}

#endif  // TRANSMIT_FUZZING_ENABLED
