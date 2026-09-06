// "Restore into this folder" has to mean it, whatever the archive says.
//
// An archive is a file. It can be edited, it can be built by something that is
// not this program, and it can be handed to somebody who has no way of knowing
// any of that. Everything below asks the same question of the two functions a
// restore actually goes through - NameSanitizer::sanitizeRelativePath and
// PathTokenMap::resolve - which is what ImportService calls for every entry:
// can what the archive claims put a file outside the folder that was chosen?

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "format/NameSanitizer.h"
#include "format/PathToken.h"

namespace transmit::format {
namespace {

constexpr const char* kDestination = "/tmp/chosen-destination";

/// The table ImportService builds for "restore into this folder": every token
/// rooted under the destination, so nothing has anywhere else to land.
PathTokenMap confinedTo(std::string_view destination, OsFamily family = OsFamily::Linux) {
    PathTokenMap map(family);
    for (const PathTokenId token : allTokens()) {
        map.setBase(token, joinPath(std::string(destination), std::string(tokenName(token))));
    }
    return map;
}

/// The two steps a restore takes between reading an entry and writing it.
std::optional<std::string> whereItLands(const PathTokenMap& map, PathTokenId token,
                                        std::string_view relative,
                                        OsFamily family = OsFamily::Linux) {
    NameSanitizer sanitizer(SanitizeOptions::forTarget(family));
    const TokenizedPath safe{token, sanitizer.sanitizeRelativePath(relative)};
    const auto resolved = map.resolve(safe);
    if (!resolved) {
        return std::nullopt;
    }
    return *resolved;
}

// Paths an archive might carry that are trying to get out of the folder they
// were pointed at. Each is either refused or lands inside; there is no third
// acceptable answer.
const std::vector<const char*>& escapeAttempts() {
    static const std::vector<const char*> attempts = {
        "../secrets",
        "../../etc/passwd",
        "../../../../../../../../etc/passwd",
        "a/../../../etc/passwd",
        "a/b/c/../../../../../../etc/passwd",
        "./../../etc/passwd",
        "..",
        "../",
        "/etc/passwd",
        "//etc/passwd",
        "/../etc/passwd",
        R"(..\..\windows\system32\drivers\etc\hosts)",
        R"(C:\Windows\System32\calc.exe)",
        R"(\\server\share\payload)",
        "~/.bashrc",
        "~root/.ssh/authorized_keys",
        ".ssh/authorized_keys",
        ".config/autostart/payload.desktop",
        "....//....//etc/passwd",
        ".../.../etc/passwd",
        "..;/etc/passwd",
        "a/./../../b",
    };
    return attempts;
}

TEST(RestoreConfinement, NothingAnArchiveSaysEscapesTheChosenFolder) {
    const PathTokenMap map = confinedTo(kDestination);
    const std::string destination = normalizePath(kDestination, OsFamily::Linux);

    for (const PathTokenId token : allTokens()) {
        for (const char* attempt : escapeAttempts()) {
            const auto landed = whereItLands(map, token, attempt);
            if (!landed) {
                continue;  // Refused outright, which is the other right answer.
            }
            EXPECT_TRUE(isWithin(destination, *landed, OsFamily::Linux))
                << "{" << tokenName(token) << "} \"" << attempt << "\" landed at " << *landed
                << ", which is outside " << destination;
        }
    }
}

// A relative answer is not an answer. ImportService writes to whatever resolve
// hands back, and a path with no root is written wherever the process happens
// to have been started - a different folder for every way of launching it.
TEST(RestoreConfinement, WhereAFileGoesNeverDependsOnTheWorkingDirectory) {
    const PathTokenMap map = confinedTo(kDestination);

    for (const PathTokenId token : allTokens()) {
        for (const char* attempt : escapeAttempts()) {
            const auto landed = whereItLands(map, token, attempt);
            if (!landed) {
                continue;
            }
            EXPECT_TRUE(landed->starts_with("/"))
                << "{" << tokenName(token) << "} \"" << attempt << "\" resolved to \"" << *landed
                << "\", which is relative to wherever this was run from";
        }
    }
}

// The same question of a machine's own folder table, which is what a restore
// with no destination override uses. An entry cannot be allowed to name a path
// that is not a path.
TEST(RestoreConfinement, AnEntryThatIsNotAnAbsolutePathIsRefusedNotGuessedAt) {
    const PathTokenMap real = PathTokenMap::defaultsFor(OsFamily::Linux, "/home/someone");

    for (const char* relative : {"etc/passwd", "opt/notes.txt", "", "."}) {
        const auto resolved = real.resolve(TokenizedPath{PathTokenId::Absolute, relative});
        EXPECT_FALSE(resolved) << "{ABS} \"" << relative << "\" resolved to " << *resolved
                               << " rather than being refused, and nothing about that path says "
                                  "where it was meant to go";
    }
}

// And what an ordinary absolute entry is for: a file that was outside every
// known folder when it was captured goes back where it came from.
TEST(RestoreConfinement, AnAbsoluteEntryStillGoesWhereItSays) {
    const PathTokenMap real = PathTokenMap::defaultsFor(OsFamily::Linux, "/home/someone");

    const auto resolved = real.resolve(TokenizedPath{PathTokenId::Absolute, "/opt/notes.txt"});
    ASSERT_TRUE(resolved);
    EXPECT_EQ(*resolved, "/opt/notes.txt");
}

// A Windows target has its own ways out: drive letters, UNC paths, and the
// alternate separator.
TEST(RestoreConfinement, TheSameHoldsForAWindowsTarget) {
    const PathTokenMap map = confinedTo("C:/chosen-destination", OsFamily::Windows);
    const std::string destination = normalizePath("C:/chosen-destination", OsFamily::Windows);

    for (const PathTokenId token : allTokens()) {
        for (const char* attempt : escapeAttempts()) {
            const auto landed = whereItLands(map, token, attempt, OsFamily::Windows);
            if (!landed) {
                continue;
            }
            EXPECT_TRUE(isWithin(destination, *landed, OsFamily::Windows))
                << "{" << tokenName(token) << "} \"" << attempt << "\" landed at " << *landed;
        }
    }
}

}  // namespace
}  // namespace transmit::format
