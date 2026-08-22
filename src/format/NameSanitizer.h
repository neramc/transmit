#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "format/PathToken.h"
#include "format/Result.h"

namespace transmit::format {

enum class RenameReason : std::uint8_t {
    None = 0,
    IllegalCharacter,   ///< < > : " / \ | ? * or a control character
    ReservedName,       ///< CON, PRN, AUX, NUL, COM1-9, LPT1-9
    TrailingDotOrSpace, ///< Windows silently strips these, so we rename first
    EmptyComponent,
    CaseCollision,      ///< two names that differ only by case on a case-blind target
    ExactCollision,
    PathTooLong,
};

std::string_view renameReasonName(RenameReason reason) noexcept;

struct RenameRecord {
    std::string original;  ///< internal '/' path as captured
    std::string applied;   ///< internal '/' path actually written
    RenameReason reason = RenameReason::None;
};

struct SanitizeOptions {
    OsFamily target = hostOsFamily();

    /// Windows and the default macOS volume compare names case-blind, so two
    /// files captured from Linux that differ only by case would overwrite one
    /// another. When true, the second one is renamed instead.
    bool caseInsensitive = false;

    /// 0 disables the check. Windows without long-path support caps a full
    /// path at 260 characters.
    std::size_t maxPathLength = 0;

    std::size_t maxComponentLength = 255;

    static SanitizeOptions forTarget(OsFamily target);
};

/// Makes captured paths safe on the restore target and keeps a record of every
/// change, so the report can show what was renamed and the path-rewriting pass
/// can fix references inside configuration files.
///
/// Instances are stateful: collision handling depends on what has already been
/// placed, so one sanitizer serves one restore run.
class NameSanitizer {
public:
    explicit NameSanitizer(SanitizeOptions options = {});

    /// Sanitizes a single component with no collision handling.
    [[nodiscard]] std::string sanitizeComponent(std::string_view component,
                                                RenameReason* reason = nullptr) const;

    /// Sanitizes a full relative path and reserves the result, so a later call
    /// that would land on the same name gets a distinct one.
    [[nodiscard]] std::string sanitizeRelativePath(std::string_view relativePath);

    /// Looks up what a previously sanitized path became. Used by the rewriting
    /// pass to repair references inside restored configuration files.
    [[nodiscard]] const std::string* appliedFor(std::string_view originalRelativePath) const;

    [[nodiscard]] const std::vector<RenameRecord>& renames() const noexcept { return renames_; }
    [[nodiscard]] const SanitizeOptions& options() const noexcept { return options_; }

    void reset();

private:
    [[nodiscard]] std::string uniqueNameIn(const std::string& parent, const std::string& candidate,
                                           RenameReason& reason);
    [[nodiscard]] static std::string foldCase(std::string_view text);

    SanitizeOptions options_;
    /// parent directory -> already used names (folded when case-insensitive)
    std::unordered_map<std::string, std::unordered_set<std::string>> used_;
    std::unordered_map<std::string, std::string> mapping_;
    std::vector<RenameRecord> renames_;
};

}  // namespace transmit::format
