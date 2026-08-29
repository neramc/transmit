#pragma once

/// A very small deterministic generator, in place of a property-testing
/// framework.
///
/// The point of a property test here is not clever shrinking: it is running
/// the same invariant over shapes nobody thought to write a case for -
/// empty files, duplicate content, names at the length limit, a file
/// exactly one byte larger than a solid block. A seeded mt19937_64 and a
/// handful of helpers cover that, and cost the project no new dependency.
///
/// Every suite prints its seed and honours TRANSMIT_PROPERTY_SEED, so a
/// failure found on a build machine is reproducible everywhere.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "format/Bytes.h"

namespace transmit::property {

/// How many cases a suite runs. Small by default so the ordinary test run
/// stays fast; the nightly job raises it.
inline int caseCount(int normal = 200) {
    if (const char* text = std::getenv("TRANSMIT_PROPERTY_CASES")) {
        const int parsed = std::atoi(text);
        if (parsed > 0) {
            return parsed;
        }
    }
    return normal;
}

/// The seed for a run. Taken from the environment when re-running a
/// failure, otherwise fixed, because a suite that picks a fresh seed every
/// time fails on unrelated commits and teaches everyone to ignore it.
inline std::uint64_t baseSeed() {
    if (const char* text = std::getenv("TRANSMIT_PROPERTY_SEED")) {
        return std::strtoull(text, nullptr, 10);
    }
    return 0x7ea11c0ffee1234full;
}

class Gen {
public:
    explicit Gen(std::uint64_t seed) : engine_(seed), seed_(seed) {}

    [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

    [[nodiscard]] std::uint64_t next() { return engine_(); }

    /// Inclusive on both ends.
    [[nodiscard]] int inRange(int low, int high) {
        return std::uniform_int_distribution<int>(low, high)(engine_);
    }

    [[nodiscard]] std::size_t size(std::size_t low, std::size_t high) {
        return std::uniform_int_distribution<std::size_t>(low, high)(engine_);
    }

    [[nodiscard]] bool chance(int percent) { return inRange(1, 100) <= percent; }

    template<typename T>
    [[nodiscard]] const T& pick(const std::vector<T>& choices) {
        return choices.at(
            static_cast<std::size_t>(inRange(0, static_cast<int>(choices.size()) - 1)));
    }

    /// Bytes that compress well, badly, or not at all, chosen at random -
    /// the ratio decides which branch of the packer and the codec runs.
    [[nodiscard]] format::ByteBuffer bytes(std::size_t length) {
        format::ByteBuffer data(length);
        const int kind = inRange(0, 2);
        if (kind == 0) {  // highly repetitive
            const auto unit = static_cast<format::Byte>(inRange('a', 'z'));
            std::fill(data.begin(), data.end(), unit);
            return data;
        }
        if (kind == 1) {  // text-shaped
            static constexpr std::string_view kWords =
                "the quick brown fox jumps over a lazy dog while transmit copies it ";
            for (std::size_t i = 0; i < length; ++i) {
                data[i] = static_cast<format::Byte>(kWords[i % kWords.size()]);
            }
            return data;
        }
        for (format::Byte& byte : data) {  // incompressible
            byte = static_cast<format::Byte>(inRange(0, 255));
        }
        return data;
    }

    /// A relative path with the shapes that break naive handling: spaces,
    /// dots, unicode, case-only differences, and the occasional name that
    /// is illegal on Windows.
    [[nodiscard]] std::string relativePath() {
        static const std::vector<std::string> kNames = {
            "notes", "Notes",      "report.txt", "report .txt", "a.b.c", "데이터",
            "café",  "with space", "UPPER",      "lower",       "CON",   "PRN",
            "nul",   "trailing.",  "dash-name",  "under_score", "0123",  "x"};
        static const std::vector<std::string> kSuffixes = {"",     ".txt",    ".json", ".TXT",
                                                           ".bin", ".tar.gz", "."};

        std::string path;
        const int depth = inRange(0, 3);
        for (int level = 0; level < depth; ++level) {
            path += pick(kNames);
            path += '/';
        }
        path += pick(kNames);
        path += pick(kSuffixes);
        return path;
    }

private:
    std::mt19937_64 engine_;
    std::uint64_t seed_ = 0;
};

}  // namespace transmit::property
