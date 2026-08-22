#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "format/Result.h"

namespace transmit::format {

enum class OsFamily : std::uint8_t {
    Unknown = 0,
    Windows = 1,
    MacOs = 2,
    Linux = 3,
};

std::string_view osFamilyName(OsFamily family) noexcept;
Result<OsFamily> osFamilyFromName(std::string_view name);

/// The OS the running process is on. Used as the default for capture, and as
/// the restore target unless the caller emulates another OS.
OsFamily hostOsFamily() noexcept;

/// Whether paths use backslashes and drive letters, and compare case-blind.
[[nodiscard]] bool usesWindowsPathStyle(OsFamily family) noexcept;

/// The meaning of a location, independent of where any particular OS puts it.
/// Storing these instead of absolute paths is what lets an archive captured on
/// one OS land in the right place on another.
enum class PathTokenId : std::uint8_t {
    Absolute = 0,  ///< no known meaning; the user maps it during restore
    Home = 1,
    Desktop = 2,
    Documents = 3,
    Downloads = 4,
    Pictures = 5,
    Music = 6,
    Videos = 7,
    AppConfig = 8,  ///< %APPDATA% | ~/Library/Application Support | ~/.config
    AppData = 9,    ///< %LOCALAPPDATA% | ~/Library/Application Support | ~/.local/share
    AppState = 10,  ///< %LOCALAPPDATA% | ~/Library/Saved Application State | ~/.local/state
    Fonts = 11,
    PublicShare = 12,
    Templates = 13,
};

std::string_view tokenName(PathTokenId token) noexcept;
Result<PathTokenId> tokenFromName(std::string_view name);
std::vector<PathTokenId> allTokens();

/// A location split into "which known folder" plus the part below it. The
/// relative part always uses '/' separators regardless of the source OS.
struct TokenizedPath {
    PathTokenId token = PathTokenId::Absolute;
    std::string relative;

    [[nodiscard]] bool isAbsoluteFallback() const noexcept { return token == PathTokenId::Absolute; }

    /// Renders as "{DOCUMENTS}/reports/q3.pdf" for manifests, logs and the UI.
    [[nodiscard]] std::string toDisplayString() const;

    friend bool operator==(const TokenizedPath& a, const TokenizedPath& b) {
        return a.token == b.token && a.relative == b.relative;
    }
};

/// Normalises a native path to the internal form: '/' separators, no trailing
/// separator, no "." or ".." components. Windows drive letters are kept as a
/// leading "C:" component and UNC prefixes are preserved.
std::string normalizePath(std::string_view path, OsFamily family);

/// Joins with '/' and normalises, tolerating empty parts.
std::string joinPath(std::string_view base, std::string_view relative);

/// Converts an internal '/' path to the separators the given OS expects.
std::string toNativePath(std::string_view path, OsFamily family);

/// The known-folder table for one machine. The Qt layer fills it from
/// QStandardPaths; tests fill it by hand, which is why this type carries no Qt
/// dependency.
class PathTokenMap {
public:
    explicit PathTokenMap(OsFamily family = hostOsFamily());

    void setBase(PathTokenId token, std::string absolutePath);
    [[nodiscard]] std::optional<std::string> base(PathTokenId token) const;
    [[nodiscard]] OsFamily family() const noexcept { return family_; }

    /// Splits an absolute path against the longest matching known folder.
    /// Falls back to PathTokenId::Absolute when nothing matches.
    [[nodiscard]] TokenizedPath tokenize(std::string_view absolutePath) const;

    /// Rebuilds an absolute path for this machine. Fails when the archive
    /// refers to a token this machine has no location for.
    [[nodiscard]] Result<std::string> resolve(const TokenizedPath& path) const;

    /// A map with plausible defaults for the given OS and home directory.
    /// Real runs override these with the platform's actual folders; this keeps
    /// the format layer testable and gives the CLI a usable fallback.
    static PathTokenMap defaultsFor(OsFamily family, std::string_view homeDirectory);

private:
    OsFamily family_ = OsFamily::Unknown;
    std::map<PathTokenId, std::string> bases_;
};

}  // namespace transmit::format
