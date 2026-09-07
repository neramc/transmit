#include "format/FileTraits.h"

namespace transmit::format {
namespace {

constexpr std::string_view kSuffix = ".icloud";

}  // namespace

bool isEvictedICloudFile(std::string_view name) noexcept {
    return !nameBehindICloudStub(name).empty();
}

std::string_view nameBehindICloudStub(std::string_view name) noexcept {
    // ".report.pdf.icloud" -> "report.pdf". Both ends have to be right: a file
    // somebody named "notes.icloud" is a file, and a hidden file called
    // ".icloud" is not a stub for anything.
    if (name.size() <= 1 + kSuffix.size() || name.front() != '.') {
        return {};
    }
    if (name.substr(name.size() - kSuffix.size()) != kSuffix) {
        return {};
    }
    return name.substr(1, name.size() - kSuffix.size() - 1);
}

bool extendedAttributeTravels(std::string_view name) noexcept {
    const auto begins = [name](std::string_view prefix) {
        return name.size() >= prefix.size() && name.substr(0, prefix.size()) == prefix;
    };

    // Refused first, so a name that matches both a refusal and an allowance is
    // refused. There is no such name today; there is nothing stopping one.
    if (begins("security.") || begins("system.") || begins("trusted.")) {
        return false;
    }
    if (name == "com.apple.quarantine" || name == "com.apple.provenance" ||
        name == "com.apple.macl") {
        return false;
    }

    // The resource fork is not a tag. It is carried, when it is carried at
    // all, as the file it is.
    if (name == "com.apple.ResourceFork" || name == "com.apple.FinderInfo") {
        return false;
    }

    if (begins("user.")) {
        return true;
    }

    // Finder tags and the Spotlight comment: what somebody typed, kept where
    // macOS keeps it.
    return begins("com.apple.metadata:");
}

}  // namespace transmit::format
