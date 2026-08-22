#include <algorithm>

#include <gtest/gtest.h>

#include "format/NameSanitizer.h"

namespace transmit::format {
namespace {

NameSanitizer windowsSanitizer() {
    return NameSanitizer(SanitizeOptions::forTarget(OsFamily::Windows));
}

NameSanitizer linuxSanitizer() {
    return NameSanitizer(SanitizeOptions::forTarget(OsFamily::Linux));
}

TEST(SanitizeComponent, ReplacesCharactersWindowsForbids) {
    auto sanitizer = windowsSanitizer();
    EXPECT_EQ(sanitizer.sanitizeComponent(R"(inv<oi>ce:2024?.pdf)"), "inv_oi_ce_2024_.pdf");
    EXPECT_EQ(sanitizer.sanitizeComponent("pipe|name"), "pipe_name");
}

TEST(SanitizeComponent, LeavesThoseCharactersAloneOnLinux) {
    auto sanitizer = linuxSanitizer();
    EXPECT_EQ(sanitizer.sanitizeComponent("invoice:2024?.pdf"), "invoice:2024?.pdf");
}

// Windows resolves these names to devices no matter what extension follows, so
// a file called "nul.txt" would be unopenable after a restore.
TEST(SanitizeComponent, EscapesWindowsDeviceNames) {
    auto sanitizer = windowsSanitizer();
    EXPECT_EQ(sanitizer.sanitizeComponent("NUL"), "_NUL");
    EXPECT_EQ(sanitizer.sanitizeComponent("nul.txt"), "_nul.txt");
    EXPECT_EQ(sanitizer.sanitizeComponent("COM1.log"), "_COM1.log");
    EXPECT_EQ(sanitizer.sanitizeComponent("console.txt"), "console.txt");
}

TEST(SanitizeComponent, StripsTrailingDotsAndSpacesForWindows) {
    auto sanitizer = windowsSanitizer();
    RenameReason reason = RenameReason::None;
    EXPECT_EQ(sanitizer.sanitizeComponent("report.", &reason), "report");
    EXPECT_EQ(reason, RenameReason::TrailingDotOrSpace);
    EXPECT_EQ(sanitizer.sanitizeComponent("draft "), "draft");
}

TEST(SanitizeComponent, ReportsWhyItChangedTheName) {
    auto sanitizer = windowsSanitizer();
    RenameReason reason = RenameReason::None;
    sanitizer.sanitizeComponent("a<b", &reason);
    EXPECT_EQ(reason, RenameReason::IllegalCharacter);

    reason = RenameReason::None;
    sanitizer.sanitizeComponent("ordinary.txt", &reason);
    EXPECT_EQ(reason, RenameReason::None);
}

// The scenario this exists for: a Linux home directory holding both "Notes.txt"
// and "notes.txt" restored onto Windows, where the second would silently
// overwrite the first.
TEST(SanitizeRelativePath, SeparatesNamesThatCollideOnlyByCase) {
    auto sanitizer = windowsSanitizer();

    EXPECT_EQ(sanitizer.sanitizeRelativePath("Documents/Notes.txt"), "Documents/Notes.txt");
    EXPECT_EQ(sanitizer.sanitizeRelativePath("Documents/notes.txt"), "Documents/notes~1.txt");
    EXPECT_EQ(sanitizer.sanitizeRelativePath("Documents/NOTES.TXT"), "Documents/NOTES~2.TXT");

    ASSERT_EQ(sanitizer.renames().size(), 2u);
    EXPECT_EQ(sanitizer.renames()[0].reason, RenameReason::CaseCollision);
    EXPECT_EQ(sanitizer.renames()[0].original, "Documents/notes.txt");
    EXPECT_EQ(sanitizer.renames()[0].applied, "Documents/notes~1.txt");
}

TEST(SanitizeRelativePath, KeepsCaseDistinctNamesOnLinux) {
    auto sanitizer = linuxSanitizer();

    EXPECT_EQ(sanitizer.sanitizeRelativePath("docs/Notes.txt"), "docs/Notes.txt");
    EXPECT_EQ(sanitizer.sanitizeRelativePath("docs/notes.txt"), "docs/notes.txt");
    EXPECT_TRUE(sanitizer.renames().empty());
}

// Without prefix memoisation the second file in a directory would see the
// directory name already reserved and scatter siblings across "docs",
// "docs~1", "docs~2"...
TEST(SanitizeRelativePath, ReusesTheSameDirectoryForSiblings) {
    auto sanitizer = windowsSanitizer();

    EXPECT_EQ(sanitizer.sanitizeRelativePath("projects/alpha/main.cpp"), "projects/alpha/main.cpp");
    EXPECT_EQ(sanitizer.sanitizeRelativePath("projects/alpha/util.cpp"), "projects/alpha/util.cpp");
    EXPECT_EQ(sanitizer.sanitizeRelativePath("projects/beta/main.cpp"), "projects/beta/main.cpp");

    EXPECT_TRUE(sanitizer.renames().empty());
}

TEST(SanitizeRelativePath, SanitizesEveryComponentOfAPath) {
    auto sanitizer = windowsSanitizer();
    EXPECT_EQ(sanitizer.sanitizeRelativePath("re:ports/AUX/q1?.txt"), "re_ports/_AUX/q1_.txt");

    ASSERT_FALSE(sanitizer.renames().empty());
    EXPECT_EQ(sanitizer.renames().front().reason, RenameReason::IllegalCharacter);
}

TEST(SanitizeRelativePath, IsStableWhenCalledTwiceForTheSamePath) {
    auto sanitizer = windowsSanitizer();
    const std::string first = sanitizer.sanitizeRelativePath("Documents/notes.txt");
    const std::string second = sanitizer.sanitizeRelativePath("Documents/notes.txt");
    EXPECT_EQ(first, second);
}

TEST(SanitizeRelativePath, RecordsTheMappingForLaterPathRewriting) {
    auto sanitizer = windowsSanitizer();
    sanitizer.sanitizeRelativePath("Documents/Notes.txt");
    sanitizer.sanitizeRelativePath("Documents/notes.txt");

    const std::string* applied = sanitizer.appliedFor("Documents/notes.txt");
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(*applied, "Documents/notes~1.txt");
    EXPECT_EQ(sanitizer.appliedFor("Documents/absent.txt"), nullptr);
}

TEST(SanitizeComponent, TruncatesOnAUtf8BoundaryNotMidCharacter) {
    SanitizeOptions options = SanitizeOptions::forTarget(OsFamily::Linux);
    options.maxComponentLength = 10;
    NameSanitizer sanitizer(options);

    // Each Korean syllable is three UTF-8 bytes; a naive cut at 10 bytes would
    // leave a broken trailing sequence.
    const std::string name = "가나다라마바사";
    const std::string result = sanitizer.sanitizeComponent(name);
    EXPECT_LE(result.size(), 10u);
    EXPECT_EQ(result.size() % 3, 0u);
    EXPECT_EQ(result, "가나다");
}

TEST(Reset, ClearsCollisionStateBetweenRuns) {
    auto sanitizer = windowsSanitizer();
    sanitizer.sanitizeRelativePath("a/Notes.txt");
    sanitizer.sanitizeRelativePath("a/notes.txt");
    ASSERT_EQ(sanitizer.renames().size(), 1u);

    sanitizer.reset();
    EXPECT_TRUE(sanitizer.renames().empty());
    EXPECT_EQ(sanitizer.sanitizeRelativePath("a/notes.txt"), "a/notes.txt");
}

}  // namespace
}  // namespace transmit::format
