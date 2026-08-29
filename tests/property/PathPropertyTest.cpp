/// The properties that decide whether a restore stays inside the
/// folder it was pointed at.
///
/// Tokenisation and name sanitisation are the two places where a
/// path from another machine turns into a path on this one. A case
/// nobody wrote is exactly where a "../.." would get through, so
/// these run over generated paths rather than chosen ones.

#include <set>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "format/NameSanitizer.h"
#include "format/PathToken.h"
#include "property/Generators.h"

namespace transmit::format {
namespace {

using property::Gen;

const std::vector<OsFamily>& everyFamily() {
    static const std::vector<OsFamily> kFamilies = {OsFamily::Windows, OsFamily::MacOs,
                                                     OsFamily::Linux};
    return kFamilies;
}

std::string homeFor(OsFamily family) {
    switch (family) {
        case OsFamily::Windows:
            return "C:/Users/bob";
        case OsFamily::MacOs:
            return "/Users/bob";
        default:
            return "/home/bob";
    }
}

TEST(PathProperty, TokenisingAnAbsolutePathAndResolvingItGivesItBack) {
    Gen gen(property::baseSeed());
    SCOPED_TRACE("TRANSMIT_PROPERTY_SEED=" + std::to_string(gen.seed()));

    for (int i = 0; i < property::caseCount(400); ++i) {
        const OsFamily family = gen.pick(everyFamily());
        const PathTokenMap map = PathTokenMap::defaultsFor(family, homeFor(family));

        const std::string base = homeFor(family);
        const std::string absolute = base + "/" + gen.relativePath();

        const TokenizedPath tokenized = map.tokenize(absolute);
        const auto resolved = map.resolve(tokenized);

        ASSERT_TRUE(resolved) << absolute << ": " << resolved.error().toString();
        EXPECT_EQ(*resolved, absolute) << "token " << static_cast<int>(tokenized.token);
    }
}

/// Splits on '/' so the check is on components. "trailing.." is a
/// perfectly ordinary name; only a component that *is* ".." climbs.
std::vector<std::string> componentsOf(std::string_view path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t next = path.find('/', start);
        const std::size_t end = (next == std::string_view::npos) ? path.size() : next;
        parts.emplace_back(path.substr(start, end - start));
        if (next == std::string_view::npos) {
            break;
        }
        start = next + 1;
    }
    return parts;
}

TEST(PathProperty, ARestoredPathNeverEscapesTheFolderItResolvesUnder) {
    Gen gen(property::baseSeed() + 1);
    SCOPED_TRACE("TRANSMIT_PROPERTY_SEED=" + std::to_string(gen.seed()));

    // The shapes an archive would carry to climb out of the destination,
    // mixed in with ordinary generated ones.
    const std::vector<std::string> kEscapes = {
        "../outside",       "../../outside", "a/../../outside", "./../outside",
        "a/b/../../../out", "/absolute",     "//server/share",  "C:/elsewhere",
        "..\\windows",      "a\\..\\..\\out"};

    for (int i = 0; i < property::caseCount(400); ++i) {
        const OsFamily family = gen.pick(everyFamily());
        NameSanitizer sanitizer(SanitizeOptions::forTarget(family));

        std::string relative;
        if (gen.chance(50)) {
            relative = gen.pick(kEscapes);
        } else {
            relative = gen.relativePath();
            if (gen.chance(50)) {
                relative = "../" + relative;
            }
        }

        const std::string safe = sanitizer.sanitizeRelativePath(relative);
        SCOPED_TRACE("\"" + relative + "\" -> \"" + safe + "\"");

        ASSERT_FALSE(safe.empty());
        EXPECT_NE(safe.front(), '/') << "became absolute";
        for (const std::string& part : componentsOf(safe)) {
            EXPECT_NE(part, "..") << "kept a component that climbs";
        }

        // The property that actually matters: whatever the archive said,
        // the file lands inside the folder it was pointed at.
        const PathTokenMap map = PathTokenMap::defaultsFor(family, homeFor(family));
        const auto base = map.base(PathTokenId::Documents);
        ASSERT_TRUE(base.has_value());

        const auto resolved = map.resolve(TokenizedPath{PathTokenId::Documents, safe});
        ASSERT_TRUE(resolved) << resolved.error().toString();
        EXPECT_EQ(resolved->rfind(*base, 0), 0u)
            << *resolved << " is outside " << *base;
    }
}

TEST(PathProperty, SanitisingIsIdempotent) {
    Gen gen(property::baseSeed() + 2);
    SCOPED_TRACE("TRANSMIT_PROPERTY_SEED=" + std::to_string(gen.seed()));

    for (int i = 0; i < property::caseCount(400); ++i) {
        const OsFamily family = gen.pick(everyFamily());
        const std::string relative = gen.relativePath();

        // A fresh sanitizer each time: the same input must land on the
        // same name, rather than on a collision-avoiding variant of it.
        NameSanitizer first(SanitizeOptions::forTarget(family));
        NameSanitizer second(SanitizeOptions::forTarget(family));

        const std::string once = first.sanitizeRelativePath(relative);
        const std::string twice = second.sanitizeRelativePath(once);

        EXPECT_EQ(once, twice) << "\"" << relative << "\" is not stable under sanitising";
    }
}

TEST(PathProperty, TwoNamesThatWouldCollideAreAlwaysKeptApart) {
    Gen gen(property::baseSeed() + 3);
    SCOPED_TRACE("TRANSMIT_PROPERTY_SEED=" + std::to_string(gen.seed()));

    for (int i = 0; i < property::caseCount(200); ++i) {
        SanitizeOptions options = SanitizeOptions::forTarget(OsFamily::Windows);
        options.caseInsensitive = true;
        NameSanitizer sanitizer(options);

        const int count = gen.inRange(2, 12);
        std::set<std::string> folded;
        std::set<std::string> offered;
        for (int n = 0; n < count; ++n) {
            const std::string relative = gen.relativePath();

            // The same source path asked for twice must come back the same
            // way - that is the memoisation working, not a collision.
            if (!offered.insert(relative).second) {
                continue;
            }

            const std::string safe = sanitizer.sanitizeRelativePath(relative);

            std::string key;
            key.reserve(safe.size());
            for (const char letter : safe) {
                key += static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
            }

            // Every sanitised name must be distinct once case is folded,
            // or the second file overwrites the first on arrival.
            EXPECT_TRUE(folded.insert(key).second)
                << "\"" << relative << "\" collided as \"" << safe << "\"";
        }
    }
}

}  // namespace
}  // namespace transmit::format
