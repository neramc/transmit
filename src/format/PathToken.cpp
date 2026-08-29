#include "format/PathToken.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace transmit::format {
namespace {

struct TokenName {
    PathTokenId token;
    std::string_view name;
};

constexpr std::array kTokenNames = {
    TokenName{PathTokenId::Absolute, "ABS"},        TokenName{PathTokenId::Home, "HOME"},
    TokenName{PathTokenId::Desktop, "DESKTOP"},     TokenName{PathTokenId::Documents, "DOCUMENTS"},
    TokenName{PathTokenId::Downloads, "DOWNLOADS"}, TokenName{PathTokenId::Pictures, "PICTURES"},
    TokenName{PathTokenId::Music, "MUSIC"},         TokenName{PathTokenId::Videos, "VIDEOS"},
    TokenName{PathTokenId::AppConfig, "APPCONFIG"}, TokenName{PathTokenId::AppData, "APPDATA"},
    TokenName{PathTokenId::AppState, "APPSTATE"},   TokenName{PathTokenId::Fonts, "FONTS"},
    TokenName{PathTokenId::PublicShare, "PUBLIC"},  TokenName{PathTokenId::Templates, "TEMPLATES"},
};

char lowerAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return lowerAscii(x) == lowerAscii(y);
           });
}

/// True when `prefix` covers whole components of `path` under the OS's
/// comparison rules. Guards against "/home/bob" matching "/home/bobby".
bool isPathPrefix(std::string_view path, std::string_view prefix, bool caseInsensitive) {
    if (prefix.empty() || prefix.size() > path.size()) {
        return false;
    }
    const std::string_view head = path.substr(0, prefix.size());
    const bool headMatches = caseInsensitive ? equalsIgnoreCase(head, prefix) : head == prefix;
    if (!headMatches) {
        return false;
    }
    return path.size() == prefix.size() || path[prefix.size()] == '/';
}

}  // namespace

std::string_view osFamilyName(OsFamily family) noexcept {
    switch (family) {
        case OsFamily::Windows:
            return "windows";
        case OsFamily::MacOs:
            return "macos";
        case OsFamily::Linux:
            return "linux";
        case OsFamily::Unknown:
            return "unknown";
    }
    return "unknown";
}

Result<OsFamily> osFamilyFromName(std::string_view name) {
    if (name == "windows" || name == "win")
        return OsFamily::Windows;
    if (name == "macos" || name == "mac" || name == "darwin")
        return OsFamily::MacOs;
    if (name == "linux")
        return OsFamily::Linux;
    return makeError(ErrorCode::InvalidArgument, "unknown operating system '", std::string(name),
                     "' (expected windows, macos or linux)");
}

OsFamily hostOsFamily() noexcept {
#if defined(_WIN32)
    return OsFamily::Windows;
#elif defined(__APPLE__)
    return OsFamily::MacOs;
#else
    return OsFamily::Linux;
#endif
}

bool usesWindowsPathStyle(OsFamily family) noexcept {
    return family == OsFamily::Windows;
}

std::string_view tokenName(PathTokenId token) noexcept {
    for (const auto& entry : kTokenNames) {
        if (entry.token == token) {
            return entry.name;
        }
    }
    return "ABS";
}

Result<PathTokenId> tokenFromName(std::string_view name) {
    std::string_view trimmed = name;
    if (trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}') {
        trimmed = trimmed.substr(1, trimmed.size() - 2);
    }
    for (const auto& entry : kTokenNames) {
        if (equalsIgnoreCase(entry.name, trimmed)) {
            return entry.token;
        }
    }
    return makeError(ErrorCode::InvalidArgument, "unknown path token '", std::string(name), "'");
}

std::vector<PathTokenId> allTokens() {
    std::vector<PathTokenId> tokens;
    tokens.reserve(kTokenNames.size());
    for (const auto& entry : kTokenNames) {
        tokens.push_back(entry.token);
    }
    return tokens;
}

std::string TokenizedPath::toDisplayString() const {
    std::string text = "{";
    text += tokenName(token);
    text += "}";
    if (!relative.empty()) {
        text += "/";
        text += relative;
    }
    return text;
}

std::string normalizePath(std::string_view path, OsFamily family) {
    if (path.empty()) {
        return {};
    }

    std::string working(path);
    if (usesWindowsPathStyle(family)) {
        std::replace(working.begin(), working.end(), '\\', '/');
    }

    // Preserve a UNC prefix ("//server/share") or a rooted path's leading '/'.
    std::string prefix;
    std::size_t cursor = 0;
    if (working.size() >= 2 && working[0] == '/' && working[1] == '/') {
        prefix = "//";
        cursor = 2;
    } else if (!working.empty() && working[0] == '/') {
        prefix = "/";
        cursor = 1;
    }

    std::vector<std::string_view> components;
    while (cursor <= working.size()) {
        const std::size_t next = working.find('/', cursor);
        const std::size_t end = (next == std::string::npos) ? working.size() : next;
        const std::string_view part(working.data() + cursor, end - cursor);

        if (part == "..") {
            if (!components.empty() && components.back() != "..") {
                components.pop_back();
            } else if (prefix.empty()) {
                components.push_back(part);
            }
        } else if (!part.empty() && part != ".") {
            components.push_back(part);
        }

        if (next == std::string::npos) {
            break;
        }
        cursor = next + 1;
    }

    std::string result = prefix;
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (i > 0) {
            result += '/';
        }
        result.append(components[i]);
    }

    // "C:" alone means the drive root, which needs its trailing slash back.
    if (usesWindowsPathStyle(family) && result.size() == 2 && result[1] == ':') {
        result += '/';
    }
    return result;
}

std::string joinPath(std::string_view base, std::string_view relative) {
    if (base.empty()) {
        return std::string(relative);
    }
    if (relative.empty()) {
        return std::string(base);
    }

    std::string result(base);
    if (result.back() != '/') {
        result += '/';
    }
    std::string_view tail = relative;
    while (!tail.empty() && tail.front() == '/') {
        tail.remove_prefix(1);
    }
    result.append(tail);
    return result;
}

std::string toNativePath(std::string_view path, OsFamily family) {
    std::string result(path);
    if (usesWindowsPathStyle(family)) {
        std::replace(result.begin(), result.end(), '/', '\\');
    }
    return result;
}

PathTokenMap::PathTokenMap(OsFamily family) : family_(family) {}

void PathTokenMap::setBase(PathTokenId token, std::string absolutePath) {
    if (token == PathTokenId::Absolute) {
        return;
    }
    std::string normalized = normalizePath(absolutePath, family_);
    if (normalized.empty()) {
        bases_.erase(token);
        return;
    }
    bases_[token] = std::move(normalized);
}

std::optional<std::string> PathTokenMap::base(PathTokenId token) const {
    const auto it = bases_.find(token);
    if (it == bases_.end()) {
        return std::nullopt;
    }
    return it->second;
}

TokenizedPath PathTokenMap::tokenize(std::string_view absolutePath) const {
    const std::string normalized = normalizePath(absolutePath, family_);
    const bool caseInsensitive = usesWindowsPathStyle(family_);

    // Longest match wins, so ~/Documents beats ~ and a nested known folder is
    // not swallowed by its parent.
    PathTokenId bestToken = PathTokenId::Absolute;
    std::size_t bestLength = 0;

    for (const auto& [token, basePath] : bases_) {
        if (basePath.size() < bestLength) {
            continue;
        }
        if (isPathPrefix(normalized, basePath, caseInsensitive)) {
            if (basePath.size() > bestLength ||
                (basePath.size() == bestLength && token < bestToken)) {
                bestToken = token;
                bestLength = basePath.size();
            }
        }
    }

    if (bestToken == PathTokenId::Absolute) {
        return TokenizedPath{PathTokenId::Absolute, normalized};
    }

    std::string relative = normalized.substr(std::min(bestLength + 1, normalized.size()));
    return TokenizedPath{bestToken, std::move(relative)};
}

namespace {

/// Whether `candidate` is `base` or sits under it. Both are expected to be
/// normalised already.
///
/// The comparison is on whole components: "/home/bob2" must not count as
/// being inside "/home/bob", which a plain prefix test would allow.
bool isWithin(std::string_view base, std::string_view candidate, OsFamily family) {
    if (base.empty()) {
        return true;
    }
    const bool caseBlind = usesWindowsPathStyle(family);

    std::string left(base);
    std::string right(candidate);
    if (caseBlind) {
        std::transform(left.begin(), left.end(), left.begin(), lowerAscii);
        std::transform(right.begin(), right.end(), right.begin(), lowerAscii);
    }
    while (left.size() > 1 && left.back() == '/') {
        left.pop_back();
    }

    if (right.size() < left.size() || right.compare(0, left.size(), left) != 0) {
        return false;
    }
    return right.size() == left.size() || left.back() == '/' || right[left.size()] == '/';
}

}  // namespace

Result<std::string> PathTokenMap::resolve(const TokenizedPath& path) const {
    if (path.token == PathTokenId::Absolute) {
        if (path.relative.empty()) {
            return makeError(ErrorCode::InvalidArgument, "empty absolute path");
        }
        return normalizePath(path.relative, family_);
    }

    const auto it = bases_.find(path.token);
    if (it == bases_.end()) {
        return makeError(ErrorCode::NotFound, "this machine has no location for {",
                         std::string(tokenName(path.token)), "}");
    }

    const std::string joined = joinPath(it->second, path.relative);

    // The last line of defence for "restore into this folder".
    //
    // A relative path comes out of an archive, and an archive can say
    // anything: "../../.bashrc" under {DOCUMENTS} resolves to a file two
    // levels above the folder the user pointed at. NameSanitizer refuses a
    // ".." component before it reaches here, but this is the layer that
    // actually promises the destination is respected, so it checks rather
    // than assumes.
    const std::string base = normalizePath(it->second, family_);
    const std::string resolved = normalizePath(joined, family_);
    if (!isWithin(base, resolved, family_)) {
        return makeError(ErrorCode::InvalidArgument, "\"", path.relative, "\" points outside {",
                         std::string(tokenName(path.token)), "}");
    }
    return resolved;
}

PathTokenMap PathTokenMap::defaultsFor(OsFamily family, std::string_view homeDirectory) {
    PathTokenMap map(family);
    const std::string home = normalizePath(homeDirectory, family);
    if (home.empty()) {
        return map;
    }

    map.setBase(PathTokenId::Home, home);
    map.setBase(PathTokenId::Desktop, joinPath(home, "Desktop"));
    map.setBase(PathTokenId::Documents, joinPath(home, "Documents"));
    map.setBase(PathTokenId::Downloads, joinPath(home, "Downloads"));
    map.setBase(PathTokenId::Pictures, joinPath(home, "Pictures"));
    map.setBase(PathTokenId::Music, joinPath(home, "Music"));
    map.setBase(PathTokenId::Videos, joinPath(home, "Videos"));

    switch (family) {
        case OsFamily::Windows:
            map.setBase(PathTokenId::AppConfig, joinPath(home, "AppData/Roaming"));
            map.setBase(PathTokenId::AppData, joinPath(home, "AppData/Local"));
            map.setBase(PathTokenId::AppState, joinPath(home, "AppData/Local"));
            map.setBase(PathTokenId::Fonts,
                        joinPath(home, "AppData/Local/Microsoft/Windows/Fonts"));
            map.setBase(PathTokenId::PublicShare, "C:/Users/Public");
            map.setBase(PathTokenId::Templates,
                        joinPath(home, "AppData/Roaming/Microsoft/Windows/Templates"));
            break;
        case OsFamily::MacOs:
            map.setBase(PathTokenId::AppConfig, joinPath(home, "Library/Application Support"));
            map.setBase(PathTokenId::AppData, joinPath(home, "Library/Application Support"));
            map.setBase(PathTokenId::AppState, joinPath(home, "Library/Saved Application State"));
            map.setBase(PathTokenId::Fonts, joinPath(home, "Library/Fonts"));
            map.setBase(PathTokenId::PublicShare, joinPath(home, "Public"));
            map.setBase(PathTokenId::Templates, joinPath(home, "Templates"));
            break;
        case OsFamily::Linux:
        case OsFamily::Unknown:
            map.setBase(PathTokenId::AppConfig, joinPath(home, ".config"));
            map.setBase(PathTokenId::AppData, joinPath(home, ".local/share"));
            map.setBase(PathTokenId::AppState, joinPath(home, ".local/state"));
            map.setBase(PathTokenId::Fonts, joinPath(home, ".local/share/fonts"));
            map.setBase(PathTokenId::PublicShare, joinPath(home, "Public"));
            map.setBase(PathTokenId::Templates, joinPath(home, "Templates"));
            break;
    }
    return map;
}

}  // namespace transmit::format
