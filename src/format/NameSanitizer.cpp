#include "format/NameSanitizer.h"

#include <algorithm>
#include <array>

namespace transmit::format {
namespace {

constexpr std::string_view kWindowsIllegal = "<>:\"/\\|?*";

constexpr std::array<std::string_view, 22> kReservedNames = {
    "con", "prn", "aux", "nul",
    "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
    "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};

char lowerAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool isReserved(std::string_view name) {
    // Windows treats "NUL" and "NUL.txt" alike, so only the stem is compared.
    const std::size_t dot = name.find('.');
    std::string stem(dot == std::string_view::npos ? name : name.substr(0, dot));
    std::transform(stem.begin(), stem.end(), stem.begin(), lowerAscii);
    return std::find(kReservedNames.begin(), kReservedNames.end(), stem) != kReservedNames.end();
}

/// Splits a UTF-8 aware truncation point so a multi-byte sequence is not cut
/// in half when a component exceeds the limit.
std::size_t utf8SafeLength(std::string_view text, std::size_t limit) {
    if (text.size() <= limit) {
        return text.size();
    }
    std::size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0u) == 0x80u) {
        --cut;
    }
    return cut;
}

}  // namespace

std::string_view renameReasonName(RenameReason reason) noexcept {
    switch (reason) {
        case RenameReason::None:               return "none";
        case RenameReason::IllegalCharacter:   return "illegal-character";
        case RenameReason::ReservedName:       return "reserved-name";
        case RenameReason::TrailingDotOrSpace: return "trailing-dot-or-space";
        case RenameReason::EmptyComponent:     return "empty-component";
        case RenameReason::CaseCollision:      return "case-collision";
        case RenameReason::ExactCollision:     return "exact-collision";
        case RenameReason::PathTooLong:        return "path-too-long";
    }
    return "none";
}

SanitizeOptions SanitizeOptions::forTarget(OsFamily target) {
    SanitizeOptions options;
    options.target = target;
    switch (target) {
        case OsFamily::Windows:
            options.caseInsensitive = true;
            options.maxPathLength = 260;
            break;
        case OsFamily::MacOs:
            // APFS is case-insensitive by default on the boot volume.
            options.caseInsensitive = true;
            options.maxPathLength = 1024;
            break;
        case OsFamily::Linux:
        case OsFamily::Unknown:
            options.caseInsensitive = false;
            options.maxPathLength = 4096;
            break;
    }
    return options;
}

NameSanitizer::NameSanitizer(SanitizeOptions options) : options_(options) {}

void NameSanitizer::reset() {
    used_.clear();
    mapping_.clear();
    renames_.clear();
}

std::string NameSanitizer::foldCase(std::string_view text) {
    std::string folded(text);
    std::transform(folded.begin(), folded.end(), folded.begin(), lowerAscii);
    return folded;
}

std::string NameSanitizer::sanitizeComponent(std::string_view component,
                                             RenameReason* reason) const {
    const auto note = [reason](RenameReason value) {
        if (reason != nullptr && *reason == RenameReason::None) {
            *reason = value;
        }
    };
    if (reason != nullptr) {
        *reason = RenameReason::None;
    }

    if (component.empty()) {
        note(RenameReason::EmptyComponent);
        return "_";
    }

    std::string result;
    result.reserve(component.size());

    const bool windowsRules = usesWindowsPathStyle(options_.target);
    for (const char c : component) {
        const auto raw = static_cast<unsigned char>(c);
        const bool illegal =
            raw < 0x20 || (windowsRules && kWindowsIllegal.find(c) != std::string_view::npos) ||
            (!windowsRules && c == '/');
        if (illegal) {
            note(RenameReason::IllegalCharacter);
            result += '_';
        } else {
            result += c;
        }
    }

    if (windowsRules) {
        std::size_t end = result.size();
        while (end > 0 && (result[end - 1] == '.' || result[end - 1] == ' ')) {
            --end;
        }
        if (end != result.size()) {
            note(RenameReason::TrailingDotOrSpace);
            result.resize(end);
        }
        if (result.empty()) {
            note(RenameReason::EmptyComponent);
            result = "_";
        }
        if (isReserved(result)) {
            note(RenameReason::ReservedName);
            result.insert(result.begin(), '_');
        }
    }

    if (options_.maxComponentLength > 0 && result.size() > options_.maxComponentLength) {
        note(RenameReason::PathTooLong);
        result.resize(utf8SafeLength(result, options_.maxComponentLength));
        if (result.empty()) {
            result = "_";
        }
    }

    return result;
}

std::string NameSanitizer::uniqueNameIn(const std::string& parent, const std::string& candidate,
                                        RenameReason& reason) {
    auto& taken = used_[parent];
    const auto key = [this](const std::string& name) {
        return options_.caseInsensitive ? foldCase(name) : name;
    };

    if (taken.insert(key(candidate)).second) {
        return candidate;
    }

    // Split off the extension so "photo.jpg" becomes "photo~1.jpg".
    const std::size_t dot = candidate.rfind('.');
    const bool hasExtension = dot != std::string::npos && dot != 0;
    const std::string stem = hasExtension ? candidate.substr(0, dot) : candidate;
    const std::string extension = hasExtension ? candidate.substr(dot) : std::string();

    for (unsigned suffix = 1; suffix < 100000u; ++suffix) {
        std::string attempt = stem + "~" + std::to_string(suffix) + extension;
        if (taken.insert(key(attempt)).second) {
            if (reason == RenameReason::None) {
                reason = options_.caseInsensitive ? RenameReason::CaseCollision
                                                  : RenameReason::ExactCollision;
            }
            return attempt;
        }
    }

    // Astronomically unlikely; fall back to something unique enough to proceed.
    reason = RenameReason::ExactCollision;
    return stem + "~x" + std::to_string(taken.size()) + extension;
}

std::string NameSanitizer::sanitizeRelativePath(std::string_view relativePath) {
    const std::string original(relativePath);
    if (const auto it = mapping_.find(original); it != mapping_.end()) {
        return it->second;
    }

    // Each prefix is resolved once and memoised. Without this, the second file
    // inside a directory would see the directory name already reserved and
    // rename it, scattering siblings across "Documents", "Documents~1", ...
    std::string originalPrefix;
    std::string appliedPrefix;

    std::size_t start = 0;
    while (start <= original.size()) {
        const std::size_t next = original.find('/', start);
        const std::size_t end = (next == std::string::npos) ? original.size() : next;
        const std::string_view component(original.data() + start, end - start);

        if (!component.empty() && component != ".") {
            originalPrefix = originalPrefix.empty()
                                 ? std::string(component)
                                 : originalPrefix + "/" + std::string(component);

            if (const auto cached = mapping_.find(originalPrefix); cached != mapping_.end()) {
                appliedPrefix = cached->second;
            } else {
                RenameReason reason = RenameReason::None;
                std::string safe = sanitizeComponent(component, &reason);
                safe = uniqueNameIn(appliedPrefix, safe, reason);

                const std::string appliedPath =
                    appliedPrefix.empty() ? safe : appliedPrefix + "/" + safe;

                if (options_.maxPathLength > 0 && appliedPath.size() > options_.maxPathLength &&
                    reason == RenameReason::None) {
                    reason = RenameReason::PathTooLong;
                }
                if (reason != RenameReason::None || appliedPath != originalPrefix) {
                    renames_.push_back(RenameRecord{
                        originalPrefix, appliedPath,
                        reason == RenameReason::None ? RenameReason::ExactCollision : reason});
                }
                mapping_.emplace(originalPrefix, appliedPath);
                appliedPrefix = appliedPath;
            }
        }

        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }

    mapping_[original] = appliedPrefix;
    return appliedPrefix;
}

const std::string* NameSanitizer::appliedFor(std::string_view originalRelativePath) const {
    const auto it = mapping_.find(std::string(originalRelativePath));
    return it == mapping_.end() ? nullptr : &it->second;
}

}  // namespace transmit::format
