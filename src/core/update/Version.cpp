#include "core/update/Version.h"

#include <charconv>
#include <limits>

namespace transmit::core {
namespace {

/// One numeric part. Returns nothing for an empty field, a leading zero on a
/// multi-digit number, anything that is not a digit, or a value too large.
std::optional<std::uint32_t> parsePart(std::string_view text) {
    if (text.empty() || text.size() > 10) {
        return std::nullopt;
    }
    if (text.size() > 1 && text.front() == '0') {
        return std::nullopt;
    }
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
    }

    std::uint64_t value = 0;
    const auto* const begin = text.data();
    const auto result = std::from_chars(begin, begin + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != begin + text.size()) {
        return std::nullopt;
    }
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

}  // namespace

std::optional<Version> Version::parse(std::string_view text) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
        text.remove_prefix(1);
    }

    Version version;
    std::uint32_t* const parts[] = {&version.major, &version.minor, &version.patch};

    std::size_t index = 0;
    for (auto* const part : parts) {
        const std::size_t dot = text.find('.', index);
        const bool last = part == parts[2];

        // The last part runs to the end, and there must be nothing after it.
        const std::size_t end = last ? text.size() : dot;
        if (!last && dot == std::string_view::npos) {
            return std::nullopt;
        }
        if (last && dot != std::string_view::npos) {
            return std::nullopt;
        }

        const auto value = parsePart(text.substr(index, end - index));
        if (!value) {
            return std::nullopt;
        }
        *part = *value;
        index = end + 1;
    }
    return version;
}

std::string Version::toString() const {
    return std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch);
}

std::optional<Version> runningVersion() {
#ifdef TRANSMIT_VERSION
    return Version::parse(TRANSMIT_VERSION);
#else
    return std::nullopt;
#endif
}

}  // namespace transmit::core
