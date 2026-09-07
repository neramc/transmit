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

}  // namespace transmit::format
