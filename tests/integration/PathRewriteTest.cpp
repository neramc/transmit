#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

#include <sqlite3.h>

#include "core/recipe/AppInventoryPayload.h"
#include "core/recipe/RecipeCatalog.h"
#include "core/recipe/StateRelocator.h"
#include "core/rewrite/PathRewriter.h"
#include "core/rewrite/PathTranslator.h"
#include "core/rewrite/RewritePlan.h"
#include "core/utils/Conversions.h"

using namespace transmit;

/// Rewriting paths inside configuration files is the most invasive thing
/// Transmit does. These tests hold it to two promises: the values that are
/// genuinely paths get repointed at this machine, and every other byte of the
/// file survives untouched.
class PathRewriteTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void translatesKnownFoldersBetweenOperatingSystems();
    void leavesPathsItDoesNotRecogniseAlone();
    void appliesRenamesRecordedByTheRestore();
    void rewritesPathsEmbeddedInLongerText();
    void aSpaceInsideAPathDoesNotEndIt();
    void aSpaceDoesNotLetAPathSwallowTheSentenceAroundIt();
    void aPathRunningIntoAUriDoesNotEatIt();

    void iniRewritePreservesCommentsAndOrdering();
    void jsonRewriteTouchesOnlyTheNamedKeys();
    void plistRewriteRepointsTheKeysItWasGiven();
    void jsonRewriteReachesInsideArraysAndObjects();
    void aRewriteThatCannotBeAppliedLeavesTheOriginalAlone();
    void textRewriteKeepsUtf16Encoding();
    void sqliteRewriteUpdatesOnlyTheNamedColumn();

    void planCanBeAppliedAndReverted();
    void keptOriginalsAreThrownAwayOnRequest();
    void firefoxProfileIsRepointedEndToEnd();
    void aMacOsFirefoxProfileIsRepointedEndToEnd();
    void theMoveStepsInTheCatalogAreCarriedOut();
    void droppingAKeyKeepsTheFilesEncoding();

    void catalogLoadsAndMatches();
    void relocatesApplicationStateToWhereTheTargetKeepsIt();
    void relocationLeavesUnknownPathsAlone();

    void aRewriterThatCannotStageReportsNothing();
    void aMalformedPlistIsNotReportedAsRewritten();
    void aRuleCannotNameAFileOutsideTheFolderItWasGiven();
    void aWildcardCannotReachOutsideEither();
    void aWildcardStillFindsWhatItIsFor();

private:
    [[nodiscard]] core::PathTranslator windowsToLinux() const;
    [[nodiscard]] core::PathTranslator macOsToLinux() const;
    void write(const QString& relative, const QByteArray& content);
    [[nodiscard]] QString path(const QString& relative) const {
        return workspace_->filePath(relative);
    }
    [[nodiscard]] QByteArray read(const QString& relative) const;

    std::unique_ptr<QTemporaryDir> workspace_;
};

void PathRewriteTest::init() {
    workspace_ = std::make_unique<QTemporaryDir>();
    QVERIFY(workspace_->isValid());
}

void PathRewriteTest::cleanup() {
    workspace_.reset();
}

void PathRewriteTest::write(const QString& relative, const QByteArray& content) {
    const QString full = path(relative);
    QDir().mkpath(QFileInfo(full).absolutePath());
    QFile file(full);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(full));
    file.write(content);
}

QByteArray PathRewriteTest::read(const QString& relative) const {
    QFile file(path(relative));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

/// A capture taken on a Windows machine, restored onto a Linux one.
core::PathTranslator PathRewriteTest::windowsToLinux() const {
    format::SourceEnvironment source;
    source.os = format::OsFamily::Windows;
    source.homeDirectory = "C:/Users/Bob";

    const auto windows =
        format::PathTokenMap::defaultsFor(format::OsFamily::Windows, "C:/Users/Bob");
    for (const format::PathTokenId token : format::allTokens()) {
        if (const auto base = windows.base(token)) {
            source.tokenBases[token] = *base;
        }
    }

    return core::PathTranslator(
        source, format::PathTokenMap::defaultsFor(format::OsFamily::Linux, "/home/bob"),
        format::OsFamily::Linux);
}

core::PathTranslator PathRewriteTest::macOsToLinux() const {
    format::SourceEnvironment source;
    source.os = format::OsFamily::MacOs;
    source.homeDirectory = "/Users/bob";

    const auto macos = format::PathTokenMap::defaultsFor(format::OsFamily::MacOs, "/Users/bob");
    for (const format::PathTokenId token : format::allTokens()) {
        if (const auto base = macos.base(token)) {
            source.tokenBases[token] = *base;
        }
    }

    return core::PathTranslator(
        source, format::PathTokenMap::defaultsFor(format::OsFamily::Linux, "/home/bob"),
        format::OsFamily::Linux);
}

void PathRewriteTest::translatesKnownFoldersBetweenOperatingSystems() {
    const core::PathTranslator translator = windowsToLinux();

    QCOMPARE(translator.translateOr(QStringLiteral(R"(C:\Users\Bob\Documents\report.pdf)")),
             QStringLiteral("/home/bob/Documents/report.pdf"));
    QCOMPARE(translator.translateOr(QStringLiteral(R"(C:\Users\Bob\AppData\Roaming\Mozilla)")),
             QStringLiteral("/home/bob/.config/Mozilla"));
    // Forward slashes are just as valid on Windows and must work too.
    QCOMPARE(translator.translateOr(QStringLiteral("C:/Users/Bob/Downloads")),
             QStringLiteral("/home/bob/Downloads"));
}

void PathRewriteTest::leavesPathsItDoesNotRecogniseAlone() {
    const core::PathTranslator translator = windowsToLinux();

    // Outside any known folder: there is nothing trustworthy to map it onto.
    QVERIFY(!translator.translate(QStringLiteral(R"(D:\Games\Steam)")).has_value());
    QVERIFY(!translator.translate(QStringLiteral(R"(C:\Windows\System32)")).has_value());
    // Not a path at all.
    QVERIFY(!translator.translate(QStringLiteral("dark")).has_value());
    QVERIFY(!translator.translate(QStringLiteral("1.5")).has_value());
    QVERIFY(!translator.translate(QString()).has_value());
}

void PathRewriteTest::appliesRenamesRecordedByTheRestore() {
    core::PathTranslator translator = windowsToLinux();
    translator.setRenames({{QStringLiteral("reports/Q1.txt"), QStringLiteral("reports/Q1~1.txt")}});

    // A file renamed on arrival must still be findable from the config that
    // refers to it.
    QCOMPARE(translator.translateOr(QStringLiteral(R"(C:\Users\Bob\Documents\reports\Q1.txt)")),
             QStringLiteral("/home/bob/Documents/reports/Q1~1.txt"));
}

void PathRewriteTest::rewritesPathsEmbeddedInLongerText() {
    const core::PathTranslator translator = windowsToLinux();

    int replacements = 0;
    const QString result = translator.translateWithin(
        QStringLiteral(R"(open C:\Users\Bob\Documents\a.txt then C:\Users\Bob\Pictures\b.png)"),
        &replacements);

    QCOMPARE(replacements, 2);
    QVERIFY(result.contains(QStringLiteral("/home/bob/Documents/a.txt")));
    QVERIFY(result.contains(QStringLiteral("/home/bob/Pictures/b.png")));
    QVERIFY2(result.startsWith(QStringLiteral("open ")), "surrounding text must survive");

    // A file:// URI keeps its scheme and gains the right separators.
    const QString uri =
        translator.translateWithin(QStringLiteral("file:///C:/Users/Bob/Documents/notes.txt"));
    QCOMPARE(uri, QStringLiteral("file:///home/bob/Documents/notes.txt"));
}

/// A space is an ordinary character in a path, and on two of the three
/// platforms it is in the address of nearly everything.
///
/// macOS keeps application state in "~/Library/Application Support"; Windows
/// keeps programs in "C:\\Program Files" and, for a long time, documents in
/// "My Documents". A path matcher that stops at the first space therefore
/// stops before it has seen the interesting part of almost every path it will
/// ever be handed - and the failure is silent, because a value that is not
/// recognised as a path is simply left alone.
void PathRewriteTest::aSpaceInsideAPathDoesNotEndIt() {
    const core::PathTranslator fromMac = macOsToLinux();

    QCOMPARE(fromMac.translateOr(
                 QStringLiteral("/Users/bob/Library/Application Support/Firefox/profiles.ini")),
             QStringLiteral("/home/bob/.config/Firefox/profiles.ini"));

    // The same value reached through a whole line of a settings file, which is
    // how the rewriters actually ask.
    int replacements = 0;
    QCOMPARE(fromMac.translateWithin(
                 QStringLiteral("/Users/bob/Library/Application Support/Firefox/Profiles/x1"),
                 &replacements),
             QStringLiteral("/home/bob/.config/Firefox/Profiles/x1"));
    QCOMPARE(replacements, 1);

    // Two spaces, and it is a folder Transmit itself declares: macOS keeps
    // window state under "Library/Saved Application State". A rule that
    // allowed one space per name reached the first of these and stopped.
    QCOMPARE(fromMac.translateWithin(QStringLiteral(
                 "/Users/bob/Library/Saved Application State/org.mozilla.firefox.savedState")),
             QStringLiteral("/home/bob/.local/state/org.mozilla.firefox.savedState"));

    const core::PathTranslator fromWindows = windowsToLinux();
    QCOMPARE(
        fromWindows.translateOr(QStringLiteral(R"(C:\Users\Bob\Documents\Tax Returns\2024.pdf)")),
        QStringLiteral("/home/bob/Documents/Tax Returns/2024.pdf"));
}

/// The other half of the same promise. Allowing spaces is only correct if a
/// path stops where the path stops: a sentence that mentions a folder must
/// come out of the rewrite with its remaining words intact, and a quoted path
/// must not eat the quote.
void PathRewriteTest::aSpaceDoesNotLetAPathSwallowTheSentenceAroundIt() {
    const core::PathTranslator fromMac = macOsToLinux();

    int replacements = 0;
    const QString sentence = fromMac.translateWithin(
        QStringLiteral("saved to /Users/bob/Documents/My Notes/a.txt and then closed"),
        &replacements);
    QCOMPARE(replacements, 1);
    QCOMPARE(sentence,
             QStringLiteral("saved to /home/bob/Documents/My Notes/a.txt and then closed"));

    // A trailing space belongs to the text, not to the name.
    QCOMPARE(fromMac.translateWithin(QStringLiteral("/Users/bob/Documents/a.txt  ")),
             QStringLiteral("/home/bob/Documents/a.txt  "));

    // A run of spaces is not a path component either.
    QCOMPARE(fromMac.translateWithin(QStringLiteral("/Users/bob/Documents  /etc")),
             QStringLiteral("/home/bob/Documents  /etc"));

    // Neither is a second path. Two paths side by side are two paths, and the
    // separator that starts the second one is not the separator that would
    // have made the space interior to the first.
    int two = 0;
    QCOMPARE(fromMac.translateWithin(
                 QStringLiteral("/Users/bob/Documents /Users/bob/Pictures/a.png"), &two),
             QStringLiteral("/home/bob/Documents /home/bob/Pictures/a.png"));
    QCOMPARE(two, 2);

    // Quotes and the characters a path may not contain still end it.
    QCOMPARE(fromMac.translateWithin(QStringLiteral(R"("/Users/bob/Documents/My Notes/a.txt")")),
             QStringLiteral(R"("/home/bob/Documents/My Notes/a.txt")"));
}

/// A value can hold a path and a URI side by side - a recent-files list is
/// often exactly that - and "file:" is made of characters a path may contain,
/// with a slash after it that reads as the next component. So one path match
/// covered both, and the two edits over the same stretch were applied one on
/// top of the other: the URI came out with its slashes eaten and its own path
/// still naming the machine the archive came from.
///
/// Both are repointed, and the URI keeps its scheme.
void PathRewriteTest::aPathRunningIntoAUriDoesNotEatIt() {
    const core::PathTranslator fromMac = macOsToLinux();

    int replacements = 0;
    QCOMPARE(fromMac.translateWithin(
                 QStringLiteral("/Users/bob/Documents/a.txt file:///Users/bob/Downloads/b.png"),
                 &replacements),
             QStringLiteral("/home/bob/Documents/a.txt file:///home/bob/Downloads/b.png"));
    QCOMPARE(replacements, 2);

    // The other order, and with a word between, neither of which ever broke -
    // they are here so a fix that only works one way round is not mistaken for
    // one that works.
    QCOMPARE(fromMac.translateWithin(
                 QStringLiteral("file:///Users/bob/Downloads/b.png /Users/bob/Documents/a.txt")),
             QStringLiteral("file:///home/bob/Downloads/b.png /home/bob/Documents/a.txt"));
    QCOMPARE(fromMac.translateWithin(QStringLiteral(
                 "/Users/bob/Documents/a.txt and file:///Users/bob/Downloads/b.png")),
             QStringLiteral("/home/bob/Documents/a.txt and file:///home/bob/Downloads/b.png"));
}

void PathRewriteTest::iniRewritePreservesCommentsAndOrdering() {
    const QByteArray original =
        "; Firefox profile index\n"
        "[General]\n"
        "StartWithLastProfile=1\n"
        "\n"
        "[Profile0]\n"
        "Name=default\n"
        "IsRelative=0\n"
        "Path=C:\\Users\\Bob\\AppData\\Roaming\\Mozilla\\Firefox\\Profiles\\abc.default\n"
        "# a trailing comment\n";
    write(QStringLiteral("profiles.ini"), original);

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.firefox");
    recipe.rewrites.push_back(core::RecipeRewriteRule{QStringLiteral("profiles.ini"),
                                                      QStringLiteral("ini"),
                                                      {QStringLiteral("Path")},
                                                      {},
                                                      1,
                                                      {},
                                                      {}});

    const core::PathTranslator translator = windowsToLinux();
    core::RewritePlan plan;
    core::PathRewriter(translator).planFor(recipe, workspace_->path(), plan);

    QCOMPARE(plan.edits().size(), 1);
    QCOMPARE(plan.apply(), 1);

    const QByteArray result = read(QStringLiteral("profiles.ini"));
    QVERIFY2(result.contains("Path=/home/bob/.config/Mozilla/Firefox/Profiles/abc.default"),
             result.constData());
    QVERIFY2(result.contains("; Firefox profile index"), "comments must survive");
    QVERIFY2(result.contains("# a trailing comment"), "comments must survive");
    QVERIFY2(result.contains("StartWithLastProfile=1"), "other keys must be untouched");
    QVERIFY2(result.indexOf("[General]") < result.indexOf("[Profile0]"), "order must survive");
}

// Property lists are how macOS keeps preferences, so this is the rewriter that
// runs on every restore onto a Mac. Nothing had ever exercised it, and it did
// nothing: the key name was never captured, so no value ever matched, so every
// preference restored onto a Mac kept the paths of the machine it came from
// while the restore reported success.
// "A key naming an array or object has every string inside it considered" is
// what the rewriters' contract promises. Nothing checked it, and the same
// promise turned out to be false for property lists - so it is worth asking of
// each format rather than assuming the sentence covers them all.
// The one outcome that must never happen: a settings file that was going to be
// repointed ends up neither repointed nor as it was. The original is copied
// aside rather than moved for exactly this reason, so there is no moment when
// the only copy of somebody's settings is under a name they have never heard
// of - and if the copy cannot be made, nothing is touched at all.
void PathRewriteTest::aRewriteThatCannotBeAppliedLeavesTheOriginalAlone() {
    const QByteArray original = R"({"download": {"default_directory": "C:\\Users\\Bob\\Downloads"}}
)";
    write(QStringLiteral("Preferences"), original);

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.chrome");
    recipe.rewrites.push_back(
        core::RecipeRewriteRule{QStringLiteral("Preferences"),
                                QStringLiteral("json"),
                                {QStringLiteral("download.default_directory")},
                                {},
                                1,
                                {},
                                {}});

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux()).planFor(recipe, workspace_->path(), plan);
    QCOMPARE(plan.edits().size(), 1);

    // A directory where the copy wants to put the original. QFile will not
    // remove a directory and will not copy over one, so setting the original
    // aside fails - which is the branch that decides whether a file somebody
    // owns survives a rewrite that cannot be finished. A directory rather than
    // a permission because this has to fail the same way whoever runs it, and
    // root is not stopped by permissions.
    const QString target = QDir(workspace_->path()).filePath(QStringLiteral("Preferences"));
    QVERIFY(QDir().mkpath(target + QStringLiteral(".transmit-backup")));

    QStringList errors;
    QCOMPARE(plan.apply(&errors), 0);

    QVERIFY2(!errors.isEmpty(), "a rewrite that could not be applied said nothing about it");
    QVERIFY2(errors.first().contains(QStringLiteral("Preferences")), qPrintable(errors.first()));

    // What the user has is exactly what they had.
    QCOMPARE(read(QStringLiteral("Preferences")), original);

    // And nothing is left lying about next to it claiming to be a newer
    // version, which the next run would otherwise apply without checking.
    QVERIFY2(!QFile::exists(target + QStringLiteral(".transmit-staged")),
             "a staged rewrite outlived the attempt to apply it");
}

void PathRewriteTest::jsonRewriteReachesInsideArraysAndObjects() {
    const QByteArray original = R"({
  "recent": ["C:\\Users\\Bob\\Documents\\a.txt", 7, "C:\\Users\\Bob\\Documents\\b.txt"],
  "workspace": {
    "root": "C:\\Users\\Bob\\Documents\\project",
    "nested": { "deeper": "C:\\Users\\Bob\\Documents\\deep" },
    "count": 3
  },
  "elsewhere": ["C:\\Users\\Bob\\Documents\\untouched"]
}
)";
    write(QStringLiteral("settings.json"), original);

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.arrays");
    recipe.rewrites.push_back(
        core::RecipeRewriteRule{QStringLiteral("settings.json"),
                                QStringLiteral("json"),
                                {QStringLiteral("recent"), QStringLiteral("workspace")},
                                {},
                                1,
                                {},
                                {}});

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux()).planFor(recipe, workspace_->path(), plan);

    // Two strings in the array, one at the object's root, one two levels down.
    // The number in the array and the number in the object are not strings and
    // are not edits.
    QCOMPARE(plan.edits().size(), 4);
    QCOMPARE(plan.apply(), 1);

    const QJsonObject result =
        QJsonDocument::fromJson(read(QStringLiteral("settings.json"))).object();
    const QJsonArray recent = result.value("recent").toArray();
    QCOMPARE(recent.at(0).toString(), QStringLiteral("/home/bob/Documents/a.txt"));
    QCOMPARE(recent.at(1).toInt(), 7);
    QCOMPARE(recent.at(2).toString(), QStringLiteral("/home/bob/Documents/b.txt"));

    const QJsonObject workspace = result.value("workspace").toObject();
    QCOMPARE(workspace.value("root").toString(), QStringLiteral("/home/bob/Documents/project"));
    QCOMPARE(workspace.value("nested").toObject().value("deeper").toString(),
             QStringLiteral("/home/bob/Documents/deep"));
    QCOMPARE(workspace.value("count").toInt(), 3);

    // A path under a key the rule did not name is left alone, however deep.
    QCOMPARE(result.value("elsewhere").toArray().at(0).toString(),
             QStringLiteral("C:\\Users\\Bob\\Documents\\untouched"));
}

void PathRewriteTest::plistRewriteRepointsTheKeysItWasGiven() {
    const QByteArray original =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "\t<key>DownloadFolder</key>\n"
        "\t<string>C:\\Users\\Bob\\Downloads</string>\n"
        // A key whose text arrives in more than one piece. A plist is XML and
        // may come from any writer, and a reader hands a CDATA section back
        // as its own run of characters - here "Recent ", "& Old", " Files" -
        // so a rewriter that remembers only the last piece looks for
        // " Files", finds nothing, and leaves the paths under this key alone.
        "\t<key>Recent <![CDATA[& Old]]> Files</key>\n"
        "\t<array>\n"
        "\t\t<string>C:\\Users\\Bob\\Documents\\a.txt</string>\n"
        "\t\t<string>C:\\Users\\Bob\\Documents\\b.txt</string>\n"
        "\t</array>\n"
        "\t<key>LeaveThisAlone</key>\n"
        "\t<string>C:\\Users\\Bob\\Documents\\keep-me</string>\n"
        "\t<key>WindowCount</key>\n"
        "\t<integer>3</integer>\n"
        "\t<key>Enabled</key>\n"
        "\t<true/>\n"
        "</dict>\n"
        "</plist>\n";
    write(QStringLiteral("com.example.app.plist"), original);

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.plist");
    recipe.rewrites.push_back(core::RecipeRewriteRule{
        QStringLiteral("com.example.app.plist"),
        QStringLiteral("plist"),
        {QStringLiteral("DownloadFolder"), QStringLiteral("Recent & Old Files")},
        {},
        1,
        {},
        {}});

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux()).planFor(recipe, workspace_->path(), plan);

    // Three: the download folder and both entries of the array. A key naming
    // an array has every string inside it considered, the same as for JSON.
    QCOMPARE(plan.edits().size(), 3);
    QCOMPARE(plan.apply(), 1);

    const QByteArray result = read(QStringLiteral("com.example.app.plist"));
    QVERIFY2(result.contains("<string>/home/bob/Downloads</string>"), result.constData());
    QVERIFY2(result.contains("<string>/home/bob/Documents/a.txt</string>"), result.constData());
    QVERIFY2(result.contains("<string>/home/bob/Documents/b.txt</string>"), result.constData());

    // A path under a key the rule did not name stays exactly as it was, and a
    // value that is not a string is not touched either.
    QVERIFY2(result.contains("keep-me"), result.constData());
    QVERIFY2(!result.contains("<string>/home/bob/Documents/keep-me</string>"), result.constData());
    QVERIFY2(result.contains("<integer>3</integer>"), result.constData());
    QVERIFY2(result.contains("<key>LeaveThisAlone</key>"), result.constData());

    // The keys have to survive intact, or the preferences are rewritten into
    // something the application cannot read.
    for (const char* key : {"DownloadFolder", "Recent &amp; Old Files", "WindowCount", "Enabled"}) {
        QVERIFY2(result.contains(QByteArray("<key>") + key + "</key>"), key);
    }

    // And what is written back has to be a property list, not merely a file
    // with the right words in it. An unreadable preferences file is worse for
    // the user than one that points somewhere stale.
    QXmlStreamReader check(result);
    while (!check.atEnd()) {
        check.readNext();
    }
    QVERIFY2(!check.hasError(), qPrintable(check.errorString()));
    QVERIFY2(result.contains("<!DOCTYPE plist"), "the doctype must survive");
    QVERIFY2(result.contains("<true/>"), "an empty element must survive");
}

void PathRewriteTest::jsonRewriteTouchesOnlyTheNamedKeys() {
    const QByteArray original = R"({
  "download": {
    "default_directory": "C:\\Users\\Bob\\Downloads",
    "prompt_for_download": false
  },
  "untouched": {
    "some_path": "C:\\Users\\Bob\\Documents\\keep-me"
  },
  "profile": { "name": "Bob" }
}
)";
    write(QStringLiteral("Preferences"), original);

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.chrome");
    recipe.rewrites.push_back(
        core::RecipeRewriteRule{QStringLiteral("Preferences"),
                                QStringLiteral("json"),
                                {QStringLiteral("download.default_directory")},
                                {},
                                1,
                                {},
                                {}});

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux()).planFor(recipe, workspace_->path(), plan);

    QCOMPARE(plan.edits().size(), 1);
    QCOMPARE(plan.apply(), 1);

    const QJsonObject result =
        QJsonDocument::fromJson(read(QStringLiteral("Preferences"))).object();
    QCOMPARE(result.value("download").toObject().value("default_directory").toString(),
             QStringLiteral("/home/bob/Downloads"));
    QCOMPARE(result.value("download").toObject().value("prompt_for_download").toBool(), false);
    // A path under a key the rule did not name stays exactly as it was.
    QCOMPARE(result.value("untouched").toObject().value("some_path").toString(),
             QStringLiteral("C:\\Users\\Bob\\Documents\\keep-me"));
    QCOMPARE(result.value("profile").toObject().value("name").toString(), QStringLiteral("Bob"));
}

void PathRewriteTest::textRewriteKeepsUtf16Encoding() {
    // Windows applications commonly write UTF-16 with a byte order mark; a
    // rewrite that silently converted it to UTF-8 would break the reader.
    const QString source = QStringLiteral(
        "user_pref(\"browser.download.dir\", \"C:\\\\Users\\\\Bob\\\\Downloads\");\n"
        "user_pref(\"intl.locale\", \"ko-KR\");\n");
    QByteArray utf16;
    utf16.append("\xFF\xFE", 2);
    for (const QChar c : source) {
        const ushort unit = c.unicode();
        utf16.append(static_cast<char>(unit & 0xFF));
        utf16.append(static_cast<char>(unit >> 8));
    }
    write(QStringLiteral("prefs.js"), utf16);

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.firefox");
    recipe.rewrites.push_back(core::RecipeRewriteRule{
        QStringLiteral("prefs.js"),
        QStringLiteral("text"),
        {},
        QStringLiteral(R"re(user_pref\("browser\.download\.dir", "([^"]*)"\))re"),
        1,
        {},
        {}});

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux()).planFor(recipe, workspace_->path(), plan);

    QCOMPARE(plan.edits().size(), 1);
    QCOMPARE(plan.apply(), 1);

    const QByteArray result = read(QStringLiteral("prefs.js"));
    QVERIFY2(result.startsWith("\xFF\xFE"), "the byte order mark must survive");

    const QString decoded = QString::fromUtf16(
        reinterpret_cast<const char16_t*>(result.constData() + 2), (result.size() - 2) / 2);
    QVERIFY2(decoded.contains(QStringLiteral("/home/bob/Downloads")), qPrintable(decoded));
    QVERIFY2(decoded.contains(QStringLiteral("ko-KR")), "other preferences must be untouched");
}

void PathRewriteTest::sqliteRewriteUpdatesOnlyTheNamedColumn() {
    const QString databasePath = path(QStringLiteral("places.sqlite"));
    sqlite3* handle = nullptr;
    QCOMPARE(sqlite3_open(databasePath.toUtf8().constData(), &handle), SQLITE_OK);
    sqlite3_exec(handle,
                 "CREATE TABLE recent (id INTEGER PRIMARY KEY, location TEXT, title TEXT);"
                 "INSERT INTO recent VALUES (1, 'C:\\Users\\Bob\\Documents\\a.txt', 'C:\\keep');"
                 "INSERT INTO recent VALUES (2, 'C:\\Users\\Bob\\Pictures\\b.png', 'photo');"
                 "INSERT INTO recent VALUES (3, 'not a path', 'plain');",
                 nullptr, nullptr, nullptr);
    sqlite3_close(handle);

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.places");
    recipe.rewrites.push_back(core::RecipeRewriteRule{QStringLiteral("places.sqlite"),
                                                      QStringLiteral("sqlite"),
                                                      {},
                                                      {},
                                                      1,
                                                      QStringLiteral("recent"),
                                                      QStringLiteral("location")});

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux()).planFor(recipe, workspace_->path(), plan);

    QCOMPARE(plan.edits().size(), 2);
    QCOMPARE(plan.apply(), 1);

    QCOMPARE(sqlite3_open(databasePath.toUtf8().constData(), &handle), SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    sqlite3_prepare_v2(handle, "SELECT location, title FROM recent ORDER BY id", -1, &statement,
                       nullptr);

    QStringList locations;
    QStringList titles;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        locations << QString::fromUtf8(
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)));
        titles << QString::fromUtf8(
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)));
    }
    sqlite3_finalize(statement);
    sqlite3_close(handle);

    QCOMPARE(locations.at(0), QStringLiteral("/home/bob/Documents/a.txt"));
    QCOMPARE(locations.at(1), QStringLiteral("/home/bob/Pictures/b.png"));
    QCOMPARE(locations.at(2), QStringLiteral("not a path"));
    // The title column was never named by the rule, so it keeps its value.
    QCOMPARE(titles.at(0), QStringLiteral("C:\\keep"));
}

void PathRewriteTest::planCanBeAppliedAndReverted() {
    const QByteArray original = "[General]\nPath=C:\\Users\\Bob\\Documents\\x\n";
    write(QStringLiteral("settings.ini"), original);

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.app");
    recipe.rewrites.push_back(core::RecipeRewriteRule{QStringLiteral("settings.ini"),
                                                      QStringLiteral("ini"),
                                                      {QStringLiteral("Path")},
                                                      {},
                                                      1,
                                                      {},
                                                      {}});

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux()).planFor(recipe, workspace_->path(), plan);
    QVERIFY(!plan.isEmpty());

    // Building the plan must not have changed anything yet.
    QCOMPARE(read(QStringLiteral("settings.ini")), original);

    QCOMPARE(plan.apply(), 1);
    QVERIFY(read(QStringLiteral("settings.ini")).contains("/home/bob/Documents/x"));

    QCOMPARE(plan.revert(), 1);
    QCOMPARE(read(QStringLiteral("settings.ini")), original);
}

void PathRewriteTest::keptOriginalsAreThrownAwayOnRequest() {
    // Applying keeps the pre-rewrite version beside each file, which is what
    // makes the pass reversible. Nothing used to remove them, so a restore the
    // user was perfectly happy with left .transmit-backup files sitting in
    // their configuration folders for good.
    write(QStringLiteral("settings.ini"), "[General]\nPath=C:\\Users\\Bob\\Documents\\x\n");

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.app");
    recipe.rewrites.push_back(core::RecipeRewriteRule{QStringLiteral("settings.ini"),
                                                      QStringLiteral("ini"),
                                                      {QStringLiteral("Path")},
                                                      {},
                                                      1,
                                                      {},
                                                      {}});

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux()).planFor(recipe, workspace_->path(), plan);
    QCOMPARE(plan.apply(), 1);

    const QStringList touched = plan.files();
    QCOMPARE(touched.size(), 1);

    const QString backup = touched.first() + QLatin1String(core::RewritePlan::kBackupSuffix);
    QVERIFY2(QFileInfo::exists(backup), "applying should keep the original");

    QCOMPARE(core::RewritePlan::discardBackups(touched), 1);
    QVERIFY2(!QFileInfo::exists(backup), "the kept original should be gone");

    // The rewritten file itself is untouched by the clean-up: accepting a
    // restore must not undo any part of it.
    QVERIFY(read(QStringLiteral("settings.ini")).contains("/home/bob/Documents/x"));

    // Asking twice is not an error, which matters because the undo path and
    // the accept path can both reach it.
    QCOMPARE(core::RewritePlan::discardBackups(touched), 0);
}

void PathRewriteTest::firefoxProfileIsRepointedEndToEnd() {
    // The shape a real Firefox profile takes, restored from Windows onto Linux.
    write(QStringLiteral("Firefox/profiles.ini"),
          "[Profile0]\n"
          "Name=default-release\n"
          "IsRelative=0\n"
          "Path=C:\\Users\\Bob\\AppData\\Roaming\\Mozilla\\Firefox\\Profiles\\x1.default\n");
    write(QStringLiteral("Firefox/Profiles/x1.default/prefs.js"),
          "user_pref(\"browser.download.dir\", \"C:\\\\Users\\\\Bob\\\\Downloads\");\n"
          "user_pref(\"browser.startup.homepage\", \"about:home\");\n");

    core::RecipeCatalog catalog;
    QVERIFY(catalog.loadFromFile(QStringLiteral(":/catalog/app-catalog.json")) > 0);

    const core::AppRecipe firefox = catalog.recipeById(QStringLiteral("org.mozilla.firefox"));
    QVERIFY2(firefox.isValid(), "the shipped catalog must describe Firefox");

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux())
        .planFor(firefox, workspace_->path() + QStringLiteral("/Firefox"), plan);

    QVERIFY2(plan.edits().size() >= 2,
             qPrintable(QStringLiteral("only %1 edits planned").arg(plan.edits().size())));
    QCOMPARE(plan.apply(), 2);

    QVERIFY(read(QStringLiteral("Firefox/profiles.ini"))
                .contains("/home/bob/.config/Mozilla/Firefox/Profiles/x1.default"));

    const QByteArray prefs = read(QStringLiteral("Firefox/Profiles/x1.default/prefs.js"));
    QVERIFY2(prefs.contains("/home/bob/Downloads"), prefs.constData());
    QVERIFY2(prefs.contains("about:home"), "unrelated preferences must survive");
}

/// The same journey, starting from macOS instead - which is where it broke.
///
/// macOS keeps this profile under "Library/Application Support/Firefox", and
/// the name of that folder has a space in it. Every path in the file therefore
/// has a space in it, and the matcher that decides what is a path used to stop
/// at the first one: the value was cut down to ".../Library/Application",
/// which is a real folder under the home token, so it was translated - and the
/// line came out naming a folder on the new machine followed by the remains of
/// the old address. Firefox reads that as one path, finds nothing, and starts
/// with an empty profile.
///
/// Nothing here asks the machine it runs on anything, which is the point. The
/// original was found by the macOS runner, three round trips after the change
/// that broke it, because every fixture described the layout of the one system
/// this is developed on.
void PathRewriteTest::aMacOsFirefoxProfileIsRepointedEndToEnd() {
    write(QStringLiteral("Firefox/profiles.ini"),
          "[Profile0]\n"
          "Name=default-release\n"
          "IsRelative=0\n"
          "Path=/Users/bob/Library/Application Support/Firefox/Profiles/x1.default\n");
    write(QStringLiteral("Firefox/Profiles/x1.default/prefs.js"),
          "user_pref(\"browser.download.dir\", \"/Users/bob/Downloads\");\n"
          "user_pref(\"browser.startup.homepage\", \"about:home\");\n");

    core::RecipeCatalog catalog;
    QVERIFY(catalog.loadFromFile(QStringLiteral(":/catalog/app-catalog.json")) > 0);
    const core::AppRecipe firefox = catalog.recipeById(QStringLiteral("org.mozilla.firefox"));
    QVERIFY2(firefox.isValid(), "the shipped catalog must describe Firefox");

    core::RewritePlan plan;
    core::PathRewriter(macOsToLinux())
        .planFor(firefox, workspace_->path() + QStringLiteral("/Firefox"), plan);
    QCOMPARE(plan.apply(), 2);

    const QByteArray index = read(QStringLiteral("Firefox/profiles.ini"));
    QVERIFY2(index.contains("Path=/home/bob/.config/Firefox/Profiles/x1.default\n"),
             index.constData());

    // And the whole of the old address is gone. Checking only for the new one
    // would pass on the mangled result, which contains it.
    QVERIFY2(!index.contains("/Users/bob"), index.constData());
    QVERIFY2(!index.contains("Application Support"), index.constData());

    const QByteArray prefs = read(QStringLiteral("Firefox/Profiles/x1.default/prefs.js"));
    QVERIFY2(prefs.contains("/home/bob/Downloads"), prefs.constData());
    QVERIFY2(prefs.contains("about:home"), "unrelated preferences must survive");
}

/// What the catalogue has been saying all along, and nothing read.
///
/// Correcting the paths inside a file is not the whole of moving it. The
/// schema has been able to say the rest since it went to version 2 - delete
/// this index, drop these settings, force this one - and forty-seven such
/// steps were parsed, carried through the archive, and acted on by nothing at
/// all. A restore reported success and left every one of them undone.
///
/// A Firefox profile is what those steps were written for, and it needs all
/// three at once:
///   - profiles.ini is forced to IsRelative=1, so the profile is found
///     wherever the folder ended up rather than at the address it had
///   - installs.ini is keyed by a hash of the old installation directory, so
///     every entry in it is stale and the file has to go
///   - prefs.js records the build that last ran and the graphics hardware it
///     was checked against; left in place, Firefox reads them and believes it
///     has already dealt with this profile
void PathRewriteTest::theMoveStepsInTheCatalogAreCarriedOut() {
    write(QStringLiteral("Firefox/profiles.ini"),
          "[Profile0]\n"
          "Name=default-release\n"
          "IsRelative=0\n"
          "Path=C:\\Users\\Bob\\AppData\\Roaming\\Mozilla\\Firefox\\Profiles\\x1.default\n");
    write(QStringLiteral("Firefox/installs.ini"),
          "[2F5A8B1C9D3E4F60]\n"
          "Default=Profiles/x1.default\n"
          "Locked=1\n");
    write(QStringLiteral("Firefox/Profiles/x1.default/prefs.js"),
          "user_pref(\"browser.startup.homepage\", \"about:home\");\n"
          "user_pref(\"browser.startup.homepage_override.buildID\", \"20240101\");\n"
          "user_pref(\"gfx.blacklist.layers.opengl\", 4);\n"
          "user_pref(\"gfx.blacklist.webgl.msaa\", 4);\n"
          "// a comment nobody asked to remove\n"
          "user_pref(\"browser.download.dir\", \"C:\\\\Users\\\\Bob\\\\Downloads\");\n");
    write(QStringLiteral("Firefox/Profiles/x1.default/compatibility.ini"),
          "[Compatibility]\nLastVersion=121.0\n");

    core::RecipeCatalog catalog;
    QVERIFY(catalog.loadFromFile(QStringLiteral(":/catalog/app-catalog.json")) > 0);
    const core::AppRecipe firefox = catalog.recipeById(QStringLiteral("org.mozilla.firefox"));
    QVERIFY2(firefox.isValid(), "the shipped catalog must describe Firefox");
    QVERIFY2(!firefox.moves.isEmpty(), "the recipe must carry the steps this is about");

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux())
        .planFor(firefox, workspace_->path() + QStringLiteral("/Firefox"), plan);
    QStringList planned;
    for (const core::RewriteEdit& e : plan.edits()) {
        planned << QStringLiteral("%1 [%2] %3")
                       .arg(QFileInfo(e.filePath).fileName(), e.location,
                            e.kind == core::EditKind::Remove ? QStringLiteral("remove")
                                                             : QStringLiteral("change"));
    }
    const int changed = plan.apply();
    QVERIFY2(changed >= 4, qPrintable(QStringLiteral("only %1 files changed; planned: %2")
                                          .arg(changed)
                                          .arg(planned.join(QStringLiteral(" | ")))));

    // Forced to relative, and the path corrected in the same file.
    const QByteArray index = read(QStringLiteral("Firefox/profiles.ini"));
    QVERIFY2(index.contains("IsRelative=1"), index.constData());
    QVERIFY2(index.contains("/home/bob/.config/Mozilla/Firefox/Profiles/x1.default"),
             index.constData());
    QVERIFY2(index.contains("Name=default-release"), "the rest of the file must survive");

    // Both indexes keyed by the old installation are gone.
    QVERIFY2(!QFile::exists(path(QStringLiteral("Firefox/installs.ini"))),
             "installs.ini is keyed by a hash of the old installation directory");
    QVERIFY2(!QFile::exists(path(QStringLiteral("Firefox/Profiles/x1.default/compatibility.ini"))),
             "compatibility.ini records where the last build lived");

    // The settings that describe the old machine are out, and the family form
    // took both of the graphics entries rather than the one named exactly.
    const QByteArray prefs = read(QStringLiteral("Firefox/Profiles/x1.default/prefs.js"));
    QVERIFY2(!prefs.contains("homepage_override.buildID"), prefs.constData());
    QVERIFY2(!prefs.contains("gfx.blacklist"), prefs.constData());

    // And nothing else was touched: the comment, the unrelated setting, and
    // the path the ordinary rewrite rules correct.
    QVERIFY2(prefs.contains("a comment nobody asked to remove"), prefs.constData());
    QVERIFY2(prefs.contains("\"browser.startup.homepage\", \"about:home\""), prefs.constData());
    QVERIFY2(prefs.contains("/home/bob/Downloads"), prefs.constData());
}

/// Taking a setting out has to leave the file in the form it arrived in, the
/// same as correcting one does. A preferences file written UTF-16 that came
/// back UTF-8 would have the right settings removed from it and be unreadable
/// to the application that wrote it.
void PathRewriteTest::droppingAKeyKeepsTheFilesEncoding() {
    const QString source = QStringLiteral(
        "user_pref(\"intl.locale\", \"ko-KR\");\n"
        "user_pref(\"gfx.blacklist.layers.opengl\", 4);\n");
    QByteArray utf16;
    utf16.append("\xFF\xFE", 2);
    for (const QChar c : source) {
        const ushort unit = c.unicode();
        utf16.append(static_cast<char>(unit & 0xFF));
        utf16.append(static_cast<char>(unit >> 8));
    }
    write(QStringLiteral("prefs.js"), utf16);

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.firefox");
    core::RecipeMoveStep step;
    step.fromOs = QStringLiteral("*");
    step.toOs = QStringLiteral("*");
    step.file = QStringLiteral("prefs.js");
    step.format = QStringLiteral("text");
    step.action = core::MoveAction::DropKeys;
    step.keys = {QStringLiteral("gfx.blacklist.")};
    recipe.moves.push_back(step);

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux()).planFor(recipe, workspace_->path(), plan);
    QCOMPARE(plan.edits().size(), 1);
    QCOMPARE(plan.apply(), 1);

    const QByteArray after = read(QStringLiteral("prefs.js"));
    QVERIFY2(after.startsWith("\xFF\xFE"), "the byte order mark went missing");
    QVERIFY2(after.contains(QByteArray("i\0n\0t\0l\0", 8)),
             "the settings that were not asked about must survive");
    QVERIFY2(!after.contains(QByteArray("g\0f\0x\0", 6)), "the setting was not removed");
}

void PathRewriteTest::catalogLoadsAndMatches() {
    core::RecipeCatalog catalog;
    QVERIFY(catalog.loadFromFile(QStringLiteral(":/catalog/app-catalog.json")) >= 70);

    QList<platform::InstalledApp> installed;
    installed.push_back({QStringLiteral("firefox"),
                         QStringLiteral("firefox"),
                         QStringLiteral("128.0"),
                         {},
                         platform::PackageSource::Apt,
                         {}});
    installed.push_back({QStringLiteral("code"),
                         QStringLiteral("code"),
                         QStringLiteral("1.90"),
                         {},
                         platform::PackageSource::Apt,
                         {}});
    installed.push_back({QStringLiteral("some-unknown-thing"),
                         QStringLiteral("Unknown"),
                         {},
                         {},
                         platform::PackageSource::Apt,
                         {}});

    const QList<core::MatchedApp> matched = catalog.match(installed, format::OsFamily::Linux);
    QStringList ids;
    for (const core::MatchedApp& match : matched) {
        ids << match.recipe.id;
    }
    QVERIFY2(ids.contains(QStringLiteral("org.mozilla.firefox")), qPrintable(ids.join(u',')));
    QVERIFY2(ids.contains(QStringLiteral("com.microsoft.vscode")), qPrintable(ids.join(u',')));
    QCOMPARE(matched.size(), 2);

    // A malformed entry must not cost the caller the whole catalog.
    core::RecipeCatalog partial;
    QCOMPARE(partial.loadFromJson(R"([{"name":"no id here"},{"id":"a.b","name":"Fine"}])"), 1);
}

/// An application looks for its settings in a different place on each system.
/// Restoring the captured location verbatim would leave the profile somewhere
/// the program never looks, which is the difference between the data arriving
/// and the application actually working.
void PathRewriteTest::relocatesApplicationStateToWhereTheTargetKeepsIt() {
    core::RecipeCatalog catalog;
    QVERIFY(catalog.loadFromFile(QStringLiteral(":/catalog/app-catalog.json")) > 0);

    core::MatchedApp firefox;
    firefox.recipe = catalog.recipeById(QStringLiteral("org.mozilla.firefox"));
    QVERIFY(firefox.recipe.isValid());

    const QList<core::InventoryEntry> inventory =
        core::decodeAppInventory(core::encodeAppInventory({firefox}));
    QCOMPARE(inventory.size(), 1);
    QCOMPARE(inventory.first().recipeId, QStringLiteral("org.mozilla.firefox"));

    const core::StateRelocator toWindows(inventory, format::OsFamily::Linux,
                                         format::OsFamily::Windows);
    QVERIFY(toWindows.hasRelocations());

    // ~/.mozilla/firefox on Linux is %APPDATA%\Mozilla\Firefox on Windows.
    const format::TokenizedPath profile{format::PathTokenId::Home, ".mozilla/firefox/profiles.ini"};
    const format::TokenizedPath moved = toWindows.relocate(profile);
    QCOMPARE(moved.token, format::PathTokenId::AppConfig);
    QCOMPARE(QString::fromStdString(moved.relative),
             QStringLiteral("Mozilla/Firefox/profiles.ini"));

    // And on to macOS, from the same capture. {APPCONFIG} rather than {HOME}
    // and the whole of "Library/Application Support": that directory is what
    // macOS's {APPCONFIG} is, and a file under it is filed by that name and no
    // other. Naming it the long way round was the defect - the files went to
    // one folder and everything that looked for them went to another - so what
    // this used to expect was the shape of the bug.
    const core::StateRelocator toMac(inventory, format::OsFamily::Linux, format::OsFamily::MacOs);
    const format::TokenizedPath onMac = toMac.relocate(profile);
    QCOMPARE(onMac.token, format::PathTokenId::AppConfig);
    QCOMPARE(QString::fromStdString(onMac.relative), QStringLiteral("Firefox/profiles.ini"));

    // Restoring onto the same system moves nothing.
    const core::StateRelocator sameOs(inventory, format::OsFamily::Linux, format::OsFamily::Linux);
    QVERIFY(!sameOs.hasRelocations());
    QCOMPARE(sameOs.relocate(profile), profile);
}

void PathRewriteTest::relocationLeavesUnknownPathsAlone() {
    core::RecipeCatalog catalog;
    QVERIFY(catalog.loadFromFile(QStringLiteral(":/catalog/app-catalog.json")) > 0);

    core::MatchedApp firefox;
    firefox.recipe = catalog.recipeById(QStringLiteral("org.mozilla.firefox"));
    const QList<core::InventoryEntry> inventory =
        core::decodeAppInventory(core::encodeAppInventory({firefox}));

    const core::StateRelocator relocator(inventory, format::OsFamily::Linux,
                                         format::OsFamily::Windows);

    // A document has nothing to do with any recipe and must not move.
    const format::TokenizedPath document{format::PathTokenId::Documents, "report.pdf"};
    QCOMPARE(relocator.relocate(document), document);

    // A directory whose name merely begins the same way is a different place.
    const format::TokenizedPath lookalike{format::PathTokenId::Home, ".mozillax/notes"};
    QCOMPARE(relocator.relocate(lookalike), lookalike);
}

/// The file a rewrite rule names comes out of the archive.
///
/// Every other thing an archive says about a path is checked before it is
/// used - a component that climbs is renamed, a token that resolves outside
/// its folder is refused, a table name that is not an identifier is refused -
/// and this one was joined to the state root and opened. What happens to a
/// file this pass names is that it is read, edited, copied aside and swapped,
/// so a rule reading "../../../.bashrc" is an edit to a file on the restoring
/// machine that nobody asked for. The shipped catalogue's schema refuses
/// "..", but a rule also arrives in an archive's application list and in an
/// overlay in the user's own configuration folder, and neither of those goes
/// past the schema.
/// A change that cannot be staged is not a change.
///
/// Each rewriter writes the new contents to "<file>.transmit-staged" and
/// RewritePlan::apply renames that over the original. All of them opened the
/// file, called write() and let the destructor close it, so a write that
/// stopped early - a full disk, a stick pulled out - left a truncated file
/// which the swap then installed, and the plan reported the change as made.
/// Asked here of every format, because the fix is one function and a format
/// that stopped calling it would look exactly like one that never did.
void PathRewriteTest::aRewriterThatCannotStageReportsNothing() {
    struct Case {
        QString file;
        QString format;
        QStringList keys;
        QByteArray content;
    };
    const QList<Case> cases = {
        {QStringLiteral("settings.ini"),
         QStringLiteral("ini"),
         {QStringLiteral("General/Path")},
         "[General]\nPath=C:\\Users\\Bob\\Documents\n"},
        {QStringLiteral("prefs.json"),
         QStringLiteral("json"),
         {QStringLiteral("home")},
         "{\"home\": \"C:\\\\Users\\\\Bob\\\\Documents\"}"},
        {QStringLiteral("notes.txt"),
         QStringLiteral("text"),
         {},
         "look in C:\\Users\\Bob\\Documents for it\n"},
        {QStringLiteral("prefs.plist"),
         QStringLiteral("plist"),
         {QStringLiteral("Path")},
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<plist version=\"1.0\"><dict><key>Path</key>"
         "<string>C:\\Users\\Bob\\Documents</string></dict></plist>\n"},
    };

    for (const Case& one : cases) {
        write(one.file, one.content);

        // A directory where the staged copy wants to go. QFile will not write
        // over one, and it fails the same way for whoever runs this - a
        // permission would not stop root.
        QVERIFY(QDir().mkpath(path(one.file) + QStringLiteral(".transmit-staged")));

        core::AppRecipe recipe;
        recipe.id = QStringLiteral("test.staging");
        recipe.rewrites.push_back(core::RecipeRewriteRule{
            one.file, one.format, one.keys, QStringLiteral("(C:[\\\\/][^\\s\"']*)"), 1, {}, {}});

        core::RewritePlan plan;
        core::PathRewriter(windowsToLinux()).planFor(recipe, workspace_->path(), plan);

        QVERIFY2(
            plan.edits().isEmpty(),
            qPrintable(QStringLiteral("%1 reported a change it could not stage").arg(one.file)));
        QCOMPARE(read(one.file), one.content);
    }
}

/// A property list the reader gives up on partway through.
///
/// The reader stops and hands back nothing, so no staged copy is written and
/// apply() passes over the file in silence - but the edits collected before it
/// gave up were still reported. So the plan said these values would be
/// repointed, the restore said it had succeeded, and the file was untouched.
void PathRewriteTest::aMalformedPlistIsNotReportedAsRewritten() {
    // Well formed up to the point where it is not: the first value is a path
    // this translator would rewrite, and then the document ends mid-element.
    write(QStringLiteral("broken.plist"),
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<plist version=\"1.0\"><dict>\n"
          "<key>Path</key><string>C:\\Users\\Bob\\Documents</string>\n"
          "<key>Other</key><string>C:\\Users\\Bob\\Pictures</stri");

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.plist");
    recipe.rewrites.push_back(
        core::RecipeRewriteRule{QStringLiteral("broken.plist"),
                                QStringLiteral("plist"),
                                {QStringLiteral("Path"), QStringLiteral("Other")},
                                {},
                                1,
                                {},
                                {}});

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux()).planFor(recipe, workspace_->path(), plan);

    QVERIFY2(plan.edits().isEmpty(), "a plist that could not be rewritten reported changes");
    QCOMPARE(plan.apply(), 0);
    QVERIFY(
        !QFile::exists(path(QStringLiteral("broken.plist")) + QStringLiteral(".transmit-staged")));
}

void PathRewriteTest::aRuleCannotNameAFileOutsideTheFolderItWasGiven() {
    const QByteArray untouched = "[General]\nPath=C:\\Users\\Bob\\Documents\n";
    write(QStringLiteral("state/inside.ini"), untouched);
    write(QStringLiteral("outside.ini"), untouched);

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.hostile");
    recipe.rewrites.push_back(core::RecipeRewriteRule{QStringLiteral("../outside.ini"),
                                                      QStringLiteral("ini"),
                                                      {QStringLiteral("Path")},
                                                      {},
                                                      1,
                                                      {},
                                                      {}});

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux()).planFor(recipe, path(QStringLiteral("state")), plan);

    QCOMPARE(plan.edits().size(), 0);
    QCOMPARE(plan.apply(), 0);
    QCOMPARE(read(QStringLiteral("outside.ini")), untouched);
    QVERIFY2(!QFile::exists(path(QStringLiteral("outside.ini.transmit-staged"))),
             "nothing may even be staged for a file outside the folder");
}

/// The other branch of the matcher walks the folder rather than joining a
/// name to it, so a pattern full of ".." cannot reach out of it - but a
/// symbolic link inside the folder can, and the archive being restored is
/// what puts the links there. QDirIterator does not descend into one unless
/// it is asked to, and this is what says so: adding FollowSymlinks for some
/// other good reason would otherwise turn "the files this rule matched" into
/// "any file on the machine the archive chose to point at".
void PathRewriteTest::aWildcardCannotReachOutsideEither() {
    const QByteArray untouched = "[General]\nPath=C:\\Users\\Bob\\Documents\n";
    write(QStringLiteral("state/inside.ini"), untouched);
    write(QStringLiteral("elsewhere/outside.ini"), untouched);

    if (!QFile::link(path(QStringLiteral("elsewhere")), path(QStringLiteral("state/link")))) {
        QSKIP("this system does not make symbolic links here");
    }

    core::AppRecipe recipe;
    recipe.id = QStringLiteral("test.hostile");
    recipe.rewrites.push_back(core::RecipeRewriteRule{QStringLiteral("**/*.ini"),
                                                      QStringLiteral("ini"),
                                                      {QStringLiteral("Path")},
                                                      {},
                                                      1,
                                                      {},
                                                      {}});

    core::RewritePlan plan;
    core::PathRewriter(windowsToLinux()).planFor(recipe, path(QStringLiteral("state")), plan);

    // The one inside was found, so the pattern did work and the count means
    // something.
    QCOMPARE(plan.fileCount(), 1);
    QCOMPARE(plan.apply(), 1);
    QCOMPARE(read(QStringLiteral("elsewhere/outside.ini")), untouched);
}

/// A check that refuses everything is not a check, it is a broken feature, so
/// the wildcards a recipe is meant to use are asked for too. "**" crosses
/// folders and "*" does not - which is the whole reason there are two of them,
/// and nothing had ever said so.
void PathRewriteTest::aWildcardStillFindsWhatItIsFor() {
    const QByteArray original = "[General]\nPath=C:\\Users\\Bob\\Documents\n";
    write(QStringLiteral("state/one.ini"), original);
    write(QStringLiteral("state/deep/two.ini"), original);

    const auto matched = [this](const QString& pattern) {
        core::AppRecipe recipe;
        recipe.id = QStringLiteral("test.globs");
        recipe.rewrites.push_back(core::RecipeRewriteRule{
            pattern, QStringLiteral("ini"), {QStringLiteral("Path")}, {}, 1, {}, {}});

        core::RewritePlan plan;
        core::PathRewriter(windowsToLinux()).planFor(recipe, path(QStringLiteral("state")), plan);
        return plan.fileCount();
    };

    QCOMPARE(matched(QStringLiteral("*.ini")), 1);
    QCOMPARE(matched(QStringLiteral("**/*.ini")), 2);
    QCOMPARE(matched(QStringLiteral("deep/two.ini")), 1);
    QCOMPARE(matched(QStringLiteral("?.ini")), 0);
    QCOMPARE(matched(QStringLiteral("one.ini")), 1);
}

QTEST_MAIN(PathRewriteTest)
#include "PathRewriteTest.moc"
