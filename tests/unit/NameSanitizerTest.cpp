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
    static_cast<void>(sanitizer.sanitizeComponent("a<b", &reason));
    EXPECT_EQ(reason, RenameReason::IllegalCharacter);

    reason = RenameReason::None;
    static_cast<void>(sanitizer.sanitizeComponent("ordinary.txt", &reason));
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
    static_cast<void>(sanitizer.sanitizeRelativePath("Documents/Notes.txt"));
    static_cast<void>(sanitizer.sanitizeRelativePath("Documents/notes.txt"));

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
    static_cast<void>(sanitizer.sanitizeRelativePath("a/Notes.txt"));
    static_cast<void>(sanitizer.sanitizeRelativePath("a/notes.txt"));
    ASSERT_EQ(sanitizer.renames().size(), 1u);

    sanitizer.reset();
    EXPECT_TRUE(sanitizer.renames().empty());
    EXPECT_EQ(sanitizer.sanitizeRelativePath("a/notes.txt"), "a/notes.txt");
}

/// A restore joins the archive's relative path onto a known folder, so a
/// component that climbs writes outside the folder the user chose. An
/// archive can claim any path it likes, so this is refused rather than
/// trusted. Found by the path property suite.
TEST(Traversal, AParentReferenceIsRenamedOnEveryPlatform) {
    for (const OsFamily target : {OsFamily::Linux, OsFamily::MacOs, OsFamily::Windows}) {
        NameSanitizer sanitizer(SanitizeOptions::forTarget(target));

        EXPECT_EQ(sanitizer.sanitizeComponent(".."), "__") << "target " << static_cast<int>(target);

        const std::string safe = sanitizer.sanitizeRelativePath("../../.bashrc");
        EXPECT_EQ(safe, "__/__/.bashrc") << "target " << static_cast<int>(target);
    }
}

/// Only the component that *is* ".." climbs. A name that merely contains
/// two dots is ordinary and must survive untouched, or a restore starts
/// renaming files nobody asked it to.
TEST(Traversal, ANameThatMerelyContainsDotsIsLeftAlone) {
    NameSanitizer sanitizer(SanitizeOptions::forTarget(OsFamily::Linux));
    EXPECT_EQ(sanitizer.sanitizeComponent("a..b"), "a..b");
    EXPECT_EQ(sanitizer.sanitizeComponent("..hidden"), "..hidden");
    EXPECT_EQ(sanitizer.sanitizeRelativePath("notes../file.txt"), "notes../file.txt");
}

/// The cut that makes a name fit can make it a parent reference.
///
/// Found by the fuzzer, and it is worth saying exactly how, because reading
/// the code will not show it: the check for ".." runs on the name that came
/// in, and the length cut runs afterwards. A name that begins ".." and
/// carries on for three hundred bytes is not a parent reference and passes
/// the check; cut to fit, it is one. The tail here is UTF-8 continuation
/// bytes, which is what walks the cut backwards far enough to land on the
/// second dot - so this is not a contrived string, it is what an archive
/// written on a machine with a different encoding looks like.
TEST(Traversal, ACutThatLandsOnDotDotDoesNotClimb) {
    for (const OsFamily target : {OsFamily::Linux, OsFamily::MacOs, OsFamily::Windows}) {
        NameSanitizer sanitizer(SanitizeOptions::forTarget(target));

        const std::string tail(253, '\x80');
        const std::string safe = sanitizer.sanitizeComponent("..\"" + tail);

        EXPECT_NE(safe, "..") << "target " << static_cast<int>(target);
        EXPECT_FALSE(safe.empty()) << "target " << static_cast<int>(target);
    }
}

/// And the same cut landing on a single dot, which is quieter and still
/// loses a file: "." names the folder it is already in, so two entries that
/// were different files arrive as one and the second overwrites the first.
TEST(Traversal, ACutThatLandsOnASingleDotDoesNotNameItsOwnFolder) {
    for (const OsFamily target : {OsFamily::Linux, OsFamily::MacOs, OsFamily::Windows}) {
        NameSanitizer sanitizer(SanitizeOptions::forTarget(target));

        const std::string tail(254, '\x80');
        EXPECT_NE(sanitizer.sanitizeComponent(".\"" + tail), ".")
            << "target " << static_cast<int>(target);
    }
}

/// Whatever the rules do to a name, they leave one that fits.
///
/// The reserved-name rule is the only one that makes a name longer, and it
/// runs after the cut, so this is the pairing that could hand back a name a
/// filesystem refuses - a failure that would arrive as a write error on
/// somebody's restore rather than as a rename in the report.
TEST(Traversal, ACutNameStillFits) {
    for (const OsFamily target : {OsFamily::Linux, OsFamily::MacOs, OsFamily::Windows}) {
        NameSanitizer sanitizer(SanitizeOptions::forTarget(target));
        const std::size_t limit = sanitizer.options().maxComponentLength;

        for (const std::string& name :
             {std::string(600, 'a'), "con" + std::string(600, '.'), "nul." + std::string(600, 'x'),
              std::string(600, ' '), std::string(600, '\x80')}) {
            const std::string safe = sanitizer.sanitizeComponent(name);
            EXPECT_LE(safe.size(), limit)
                << "target " << static_cast<int>(target) << ", name of " << name.size() << " bytes";
            EXPECT_FALSE(safe.empty()) << "target " << static_cast<int>(target);
        }
    }
}

/// A cut is a reason to tell the person about, and it used to be the only
/// one recorded when the cut also made the name illegal.
TEST(Traversal, ACutIsReported) {
    NameSanitizer sanitizer(SanitizeOptions::forTarget(OsFamily::Linux));
    RenameReason reason = RenameReason::None;
    static_cast<void>(sanitizer.sanitizeComponent(std::string(600, 'a'), &reason));
    EXPECT_EQ(reason, RenameReason::PathTooLong);
}

}  // namespace
}  // namespace transmit::format
