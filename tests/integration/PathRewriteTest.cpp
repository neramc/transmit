#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

    void iniRewritePreservesCommentsAndOrdering();
    void jsonRewriteTouchesOnlyTheNamedKeys();
    void plistRewriteRepointsTheKeysItWasGiven();
    void textRewriteKeepsUtf16Encoding();
    void sqliteRewriteUpdatesOnlyTheNamedColumn();

    void planCanBeAppliedAndReverted();
    void keptOriginalsAreThrownAwayOnRequest();
    void firefoxProfileIsRepointedEndToEnd();

    void catalogLoadsAndMatches();
    void relocatesApplicationStateToWhereTheTargetKeepsIt();
    void relocationLeavesUnknownPathsAlone();

private:
    [[nodiscard]] core::PathTranslator windowsToLinux() const;
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

    // And on to macOS, from the same capture.
    const core::StateRelocator toMac(inventory, format::OsFamily::Linux, format::OsFamily::MacOs);
    const format::TokenizedPath onMac = toMac.relocate(profile);
    QCOMPARE(onMac.token, format::PathTokenId::Home);
    QCOMPARE(QString::fromStdString(onMac.relative),
             QStringLiteral("Library/Application Support/Firefox/profiles.ini"));

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

QTEST_MAIN(PathRewriteTest)
#include "PathRewriteTest.moc"
