/// What the thing that finds paths inside a file has to promise.
///
/// A settings file is text. Before any path in it can be repointed at
/// this machine, something has to decide which stretches of that text
/// are paths at all - and that decision was made by an expression that
/// had only ever been asked about paths somebody sat down and typed.
/// The one it got wrong was the commonest path on two of the three
/// platforms: macOS keeps application state under "Library/Application
/// Support", and the space stopped the match halfway through. What came
/// out was a folder on the new machine followed by the remains of the
/// old address - a plausible path that names nothing.
///
/// So the property is stated the way the defect would have failed it: a
/// path translated on its own, and the same path translated as part of
/// a text, must name the same file. It is checked over every pair of
/// operating systems and over generated names, because "the commonest
/// path on macOS" is exactly the case nobody was going to write.

#include <QRegularExpression>
#include <QString>

#include <string>

#include <gtest/gtest.h>

#include "core/rewrite/PathTranslator.h"
#include "format/Manifest.h"
#include "format/PathToken.h"

#include "property/Generators.h"

namespace transmit::core {
namespace {

using property::Gen;

const std::vector<format::OsFamily>& everyFamily() {
    static const std::vector<format::OsFamily> kFamilies = {
        format::OsFamily::Windows, format::OsFamily::MacOs, format::OsFamily::Linux};
    return kFamilies;
}

std::string homeFor(format::OsFamily family) {
    switch (family) {
        case format::OsFamily::Windows:
            return "C:/Users/bob";
        case format::OsFamily::MacOs:
            return "/Users/bob";
        default:
            return "/home/bob";
    }
}

const char* nameOf(format::OsFamily family) {
    switch (family) {
        case format::OsFamily::Windows:
            return "Windows";
        case format::OsFamily::MacOs:
            return "macOS";
        default:
            return "Linux";
    }
}

/// A capture taken on one system, restored onto another. Which two is a
/// parameter, because that is the only thing that differs between the
/// nine journeys and the shapes that break are not the same on each.
PathTranslator translatorFor(format::OsFamily from, format::OsFamily to) {
    format::SourceEnvironment source;
    source.os = from;
    source.homeDirectory = homeFor(from);

    const format::PathTokenMap sourceMap = format::PathTokenMap::defaultsFor(from, homeFor(from));
    for (const format::PathTokenId token : format::allTokens()) {
        if (const auto base = sourceMap.base(token)) {
            source.tokenBases[token] = *base;
        }
    }
    return PathTranslator(source, format::PathTokenMap::defaultsFor(to, homeFor(to)), to);
}

/// Two paths name the same file when they differ only in which slash
/// they are written with. A path may end in a component whose spaces
/// are not followed by another component - "report .txt", the last name
/// in the path - and there the match stops at the space and carries the
/// rest through as ordinary text, separators and all. That leaves the
/// same file named a different way, which is what this smooths over,
/// and it is the whole of the difference the rule is allowed to make.
QString sameFile(const QString& path) {
    QString flat = path;
    flat.replace(u'\\', u'/');
    return flat;
}

/// An absolute path on `family`, under a folder Transmit knows about.
QString somePathOn(Gen& gen, format::OsFamily family) {
    const format::PathTokenMap map = format::PathTokenMap::defaultsFor(family, homeFor(family));
    const std::vector<format::PathTokenId> tokens = format::allTokens();

    for (int attempt = 0; attempt < 8; ++attempt) {
        if (const auto base = map.base(gen.pick(tokens))) {
            return QString::fromStdString(*base + "/" + gen.relativePath());
        }
    }
    return QString::fromStdString(homeFor(family) + "/" + gen.relativePath());
}

TEST(TranslatorProperty, APathInsideATextIsTheSamePathOnItsOwn) {
    Gen gen(property::baseSeed());
    SCOPED_TRACE("TRANSMIT_PROPERTY_SEED=" + std::to_string(gen.seed()));

    for (int i = 0; i < property::caseCount(400); ++i) {
        const format::OsFamily from = gen.pick(everyFamily());
        const format::OsFamily to = gen.pick(everyFamily());
        const PathTranslator translator = translatorFor(from, to);

        const QString path = somePathOn(gen, from);
        const QString alone = translator.translate(path).value_or(path);
        const QString within = translator.translateWithin(path);

        EXPECT_EQ(sameFile(within).toStdString(), sameFile(alone).toStdString())
            << nameOf(from) << " -> " << nameOf(to) << ": " << path.toStdString();
    }
}

TEST(TranslatorProperty, TheWordsAroundAPathAreNotPartOfIt) {
    Gen gen(property::baseSeed());
    SCOPED_TRACE("TRANSMIT_PROPERTY_SEED=" + std::to_string(gen.seed()));

    // Neither word contains a separator, so neither can be mistaken for
    // another component of the path they sit beside.
    const QString before = QStringLiteral("opened ");
    const QString after = QStringLiteral(" and then closed");

    for (int i = 0; i < property::caseCount(400); ++i) {
        const format::OsFamily from = gen.pick(everyFamily());
        const PathTranslator translator = translatorFor(from, gen.pick(everyFamily()));

        const QString path = somePathOn(gen, from);
        const QString sentence = translator.translateWithin(before + path + after);

        EXPECT_TRUE(sentence.startsWith(before)) << sentence.toStdString();
        EXPECT_TRUE(sentence.endsWith(after)) << sentence.toStdString();
    }
}

TEST(TranslatorProperty, TextThatNamesNothingComesBackUntouched) {
    Gen gen(property::baseSeed());
    SCOPED_TRACE("TRANSMIT_PROPERTY_SEED=" + std::to_string(gen.seed()));

    for (int i = 0; i < property::caseCount(400); ++i) {
        const format::OsFamily from = gen.pick(everyFamily());
        const PathTranslator translator = translatorFor(from, gen.pick(everyFamily()));

        // Relative, so nothing in it is an address on the old machine
        // however it is read.
        const QString relative = QString::fromStdString(gen.relativePath());
        int replacements = -1;
        EXPECT_EQ(translator.translateWithin(relative, &replacements).toStdString(),
                  relative.toStdString());
        EXPECT_EQ(replacements, 0) << relative.toStdString();
    }
}

}  // namespace
}  // namespace transmit::core
