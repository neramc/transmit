#include <gtest/gtest.h>

#include "format/PathToken.h"

namespace transmit::format {
namespace {

TEST(NormalizePath, CollapsesSeparatorsAndDotSegments) {
    EXPECT_EQ(normalizePath("/home//bob/./docs/", OsFamily::Linux), "/home/bob/docs");
    EXPECT_EQ(normalizePath("/home/bob/docs/../pics", OsFamily::Linux), "/home/bob/pics");
    EXPECT_EQ(normalizePath("relative/./path/", OsFamily::Linux), "relative/path");
}

TEST(NormalizePath, ConvertsWindowsSeparatorsAndKeepsTheDrive) {
    EXPECT_EQ(normalizePath(R"(C:\Users\Bob\Documents)", OsFamily::Windows),
              "C:/Users/Bob/Documents");
    EXPECT_EQ(normalizePath(R"(C:\)", OsFamily::Windows), "C:/");
    EXPECT_EQ(normalizePath(R"(\\server\share\folder)", OsFamily::Windows),
              "//server/share/folder");
}

TEST(NormalizePath, LeavesBackslashesAloneOnPosixWhereTheyAreLegalInNames) {
    // A Linux file really can be called "weird\name"; treating the backslash as
    // a separator would silently split it into two directories.
    EXPECT_EQ(normalizePath("/home/bob/weird\\name", OsFamily::Linux), "/home/bob/weird\\name");
}

TEST(Tokenize, PicksTheLongestMatchingKnownFolder) {
    const auto map = PathTokenMap::defaultsFor(OsFamily::Linux, "/home/bob");

    const auto documents = map.tokenize("/home/bob/Documents/report.pdf");
    EXPECT_EQ(documents.token, PathTokenId::Documents);
    EXPECT_EQ(documents.relative, "report.pdf");

    // ~/.config lives under the home directory, so the more specific token wins.
    const auto config = map.tokenize("/home/bob/.config/app/settings.json");
    EXPECT_EQ(config.token, PathTokenId::AppConfig);
    EXPECT_EQ(config.relative, "app/settings.json");

    const auto home = map.tokenize("/home/bob/notes.txt");
    EXPECT_EQ(home.token, PathTokenId::Home);
    EXPECT_EQ(home.relative, "notes.txt");
}

TEST(Tokenize, DoesNotMatchAPartialComponent) {
    const auto map = PathTokenMap::defaultsFor(OsFamily::Linux, "/home/bob");

    // "/home/bobby" must not be treated as living inside "/home/bob".
    const auto other = map.tokenize("/home/bobby/secret.txt");
    EXPECT_EQ(other.token, PathTokenId::Absolute);
    EXPECT_EQ(other.relative, "/home/bobby/secret.txt");
}

TEST(Tokenize, IsCaseInsensitiveOnWindows) {
    const auto map = PathTokenMap::defaultsFor(OsFamily::Windows, R"(C:\Users\Bob)");

    const auto documents = map.tokenize(R"(c:\users\bob\Documents\report.pdf)");
    EXPECT_EQ(documents.token, PathTokenId::Documents);
    EXPECT_EQ(documents.relative, "report.pdf");
}

TEST(Tokenize, FallsBackToAbsoluteForUnknownLocations) {
    const auto map = PathTokenMap::defaultsFor(OsFamily::Linux, "/home/bob");

    const auto system = map.tokenize("/opt/tooling/config.yaml");
    EXPECT_EQ(system.token, PathTokenId::Absolute);
    EXPECT_EQ(system.toDisplayString(), "{ABS}//opt/tooling/config.yaml");
}

// This is the whole point of tokenising: the same capture lands in the right
// place on each OS without the archive knowing anything about the target.
TEST(Resolve, TranslatesBetweenOperatingSystems) {
    const auto windows = PathTokenMap::defaultsFor(OsFamily::Windows, R"(C:\Users\Bob)");
    const auto linux = PathTokenMap::defaultsFor(OsFamily::Linux, "/home/bob");
    const auto macos = PathTokenMap::defaultsFor(OsFamily::MacOs, "/Users/bob");

    const auto captured = windows.tokenize(R"(C:\Users\Bob\AppData\Roaming\Mozilla\profiles.ini)");
    EXPECT_EQ(captured.token, PathTokenId::AppConfig);
    EXPECT_EQ(captured.relative, "Mozilla/profiles.ini");

    const auto onLinux = linux.resolve(captured);
    ASSERT_TRUE(onLinux) << onLinux.error().toString();
    EXPECT_EQ(*onLinux, "/home/bob/.config/Mozilla/profiles.ini");

    const auto onMac = macos.resolve(captured);
    ASSERT_TRUE(onMac) << onMac.error().toString();
    EXPECT_EQ(*onMac, "/Users/bob/Library/Application Support/Mozilla/profiles.ini");
}

TEST(Resolve, ReportsATokenTheTargetHasNoLocationFor) {
    PathTokenMap sparse(OsFamily::Linux);
    sparse.setBase(PathTokenId::Home, "/home/bob");

    const auto result = sparse.resolve(TokenizedPath{PathTokenId::Fonts, "MyFont.ttf"});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST(TokenNames, RoundTrip) {
    for (const PathTokenId token : allTokens()) {
        const auto parsed = tokenFromName(tokenName(token));
        ASSERT_TRUE(parsed) << parsed.error().toString();
        EXPECT_EQ(*parsed, token);
    }
    EXPECT_TRUE(tokenFromName("{DOCUMENTS}"));
    EXPECT_FALSE(tokenFromName("NOPE"));
}

TEST(ToNativePath, UsesTheTargetSeparator) {
    EXPECT_EQ(toNativePath("C:/Users/Bob/file.txt", OsFamily::Windows), R"(C:\Users\Bob\file.txt)");
    EXPECT_EQ(toNativePath("/home/bob/file.txt", OsFamily::Linux), "/home/bob/file.txt");
}

/// The layer that actually promises "restore into this folder". Even if a
/// path reaches here with a parent reference still in it - a hand-built
/// archive, a future caller that forgets to sanitise - resolving it must
/// refuse rather than hand back a path outside the folder.
TEST(Resolve, RefusesARelativePathThatClimbsOutOfItsFolder) {
    const PathTokenMap map = PathTokenMap::defaultsFor(OsFamily::Linux, "/home/bob");

    for (const char* relative :
         {"../.bashrc", "../../etc/passwd", "a/../../../tmp/x", "./../outside"}) {
        const auto resolved = map.resolve(TokenizedPath{PathTokenId::Documents, relative});
        EXPECT_FALSE(resolved) << relative << " resolved to "
                               << (resolved ? *resolved : std::string());
        if (!resolved) {
            EXPECT_EQ(resolved.error().code, ErrorCode::InvalidArgument);
        }
    }
}

/// The check is on whole components: a sibling folder whose name merely
/// starts with the same letters is still outside.
TEST(Resolve, ASiblingWithASharedPrefixIsStillOutside) {
    PathTokenMap map(OsFamily::Linux);
    map.setBase(PathTokenId::Documents, "/home/bob");

    EXPECT_FALSE(map.resolve(TokenizedPath{PathTokenId::Documents, "../bob2/secret"}));
    EXPECT_TRUE(map.resolve(TokenizedPath{PathTokenId::Documents, "notes/a.txt"}));
}

/// Ordinary paths keep working, including the ones that walk down and back
/// up inside the folder.
TEST(Resolve, StillResolvesPathsThatStayInside) {
    const PathTokenMap map = PathTokenMap::defaultsFor(OsFamily::Linux, "/home/bob");
    const auto base = map.base(PathTokenId::Documents);
    ASSERT_TRUE(base.has_value());

    const auto resolved = map.resolve(TokenizedPath{PathTokenId::Documents, "a/b/../c.txt"});
    ASSERT_TRUE(resolved) << resolved.error().toString();
    EXPECT_EQ(*resolved, *base + "/a/c.txt");
}

}  // namespace
}  // namespace transmit::format
