#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace transmit::core {

/// A three-part version, which is the only shape anything this project
/// publishes has ever had.
///
/// Deliberately not a general semantic-version type. Pre-release suffixes and
/// build metadata are rejected rather than parsed, because an updater that
/// half-understands "0.2.0-rc1" is worse than one that refuses it: comparing
/// pre-releases correctly is fiddly, getting it wrong offers people a
/// downgrade, and this project has never published one. If that changes, this
/// is the place to change, and every caller goes through it.
struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    /// Parses "1.2.3", or "v1.2.3" as tags are written. Returns nothing for
    /// anything else: a missing part, a fourth part, a leading zero, a
    /// negative, a number too large to hold, or any text at all.
    ///
    /// Leading zeros are refused because "0.1.0" and "0.01.0" would otherwise
    /// be different spellings of one version, and an updater that thinks two
    /// names for the same build are two builds will offer people what they
    /// already have.
    [[nodiscard]] static std::optional<Version> parse(std::string_view text);

    [[nodiscard]] std::string toString() const;

    [[nodiscard]] bool isZero() const noexcept { return major == 0 && minor == 0 && patch == 0; }

    friend auto operator<=>(const Version&, const Version&) = default;
    friend bool operator==(const Version&, const Version&) = default;
};

/// The version this build reports, from TRANSMIT_VERSION. Returns nothing if
/// the build was given something that is not a version, which is a build
/// mistake rather than a runtime one - but it reaches the updater as "I do not
/// know what I am", and the safe answer to that is to install nothing.
[[nodiscard]] std::optional<Version> runningVersion();

}  // namespace transmit::core
