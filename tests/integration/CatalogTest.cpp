// The application catalog: what it says, and whether it is internally sound.
//
// The catalog is the only part of Transmit whose content is data rather than
// code, and the only part a user can extend without building anything. Both of
// those mean it has to be checked as data: a recipe naming a state root that
// does not exist, or a path that climbs out of the folder it names, is not a
// compile error and would never be one.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include "core/recipe/AppInventoryPayload.h"
#include "core/recipe/RecipeCatalog.h"
#include "core/utils/Conversions.h"

using namespace transmit;

class CatalogTest : public QObject {
    Q_OBJECT

private slots:
    void everyRecipeHasAUniqueId();
    void everyStateRootIdIsUniqueWithinItsRecipe();
    void noPathClimbsOutOfTheFolderItNames();
    void everyStatePathStartsWithAKnownToken();
    void everyStatePathIsWrittenWithTheTokenItWouldBeReadAs();
    void aFolderNamedTheLongWayRoundIsReadAsTheShortOne();
    void everyMoveStepNamesARootThatExists();
    void everyMoveStepUsesAnActionWeImplement();
    void noTwoApplicationsClaimTheSameFolder();
    void carriesDataAgreesWithHavingState();
    void everyRecipeCanBeFoundOneWayOrTheOther();
    void nothingInsideAStateFolderIsAnAbsolutePath();
    void noStateRootNamesTheSamePlaceTwice();
    void everySavedPasswordIsMarkedAsOne();
    void aSandboxedInstallIsFoundWhenItIsNotTheFirstCandidate();
    void theOldSchemaAndTheNewProduceTheSameRecipes();
    void theInventoryPayloadSurvivesTheJourney();
    void anArchiveThatNamesAFolderTheLongWayRoundIsReadTheShortWay();
    void theGradeAndTheReasonComeFromTheSameEntry();

private:
    [[nodiscard]] static core::RecipeCatalog builtIn();
    [[nodiscard]] static QJsonArray rawEntries(const QString& path);
};

core::RecipeCatalog CatalogTest::builtIn() {
    core::RecipeCatalog catalog;
    catalog.loadFromFile(QStringLiteral(":/catalog/app-catalog.json"));
    return catalog;
}

QJsonArray CatalogTest::rawEntries(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("apps")).toArray();
}

void CatalogTest::everyRecipeHasAUniqueId() {
    const QJsonArray entries = rawEntries(QStringLiteral(":/catalog/app-catalog.json"));
    QVERIFY2(entries.size() >= 150, "the built-in catalog is much smaller than it should be");

    QSet<QString> seen;
    for (const QJsonValue& value : entries) {
        const QString id = value.toObject().value(QStringLiteral("id")).toString();
        QVERIFY2(!id.isEmpty(), "a recipe has no id");

        // A duplicate is not caught by the loader: the second silently replaces
        // the first, which is exactly what a user overlay is meant to do and
        // exactly what the built-in file must never do to itself.
        QVERIFY2(!seen.contains(id),
                 qPrintable(QStringLiteral("two recipes share the id %1").arg(id)));
        seen.insert(id);
    }
}

void CatalogTest::everyStateRootIdIsUniqueWithinItsRecipe() {
    for (const QJsonValue& value : rawEntries(QStringLiteral(":/catalog/app-catalog.json"))) {
        const QJsonObject entry = value.toObject();
        const QString appId = entry.value(QStringLiteral("id")).toString();

        QSet<QString> seen;
        for (const QJsonValue& item : entry.value(QStringLiteral("state")).toArray()) {
            const QString rootId = item.toObject().value(QStringLiteral("id")).toString();
            QVERIFY2(!rootId.isEmpty(),
                     qPrintable(QStringLiteral("%1 has a state root with no id").arg(appId)));
            QVERIFY2(!seen.contains(rootId),
                     qPrintable(QStringLiteral("%1 has two state roots called %2 - a move step "
                                               "naming it would be ambiguous")
                                    .arg(appId, rootId)));
            seen.insert(rootId);
        }
    }
}

void CatalogTest::noPathClimbsOutOfTheFolderItNames() {
    // A recipe is data, and a user overlay is data somebody else wrote. A path
    // with ".." in it would resolve outside the folder the recipe named, which
    // for a capture means reading somewhere it was not given permission to and
    // for a restore means writing there.
    const core::RecipeCatalog catalog = builtIn();
    for (const core::AppRecipe& recipe : catalog.recipes()) {
        for (const core::RecipeStatePath& root : recipe.state) {
            for (auto it = root.candidatesByOs.constBegin(); it != root.candidatesByOs.constEnd();
                 ++it) {
                for (const QString& candidate : it.value()) {
                    QVERIFY2(!candidate.contains(QStringLiteral("..")),
                             qPrintable(QStringLiteral("%1: %2").arg(recipe.id, candidate)));
                }
            }
            for (const core::RecipeContent& content : root.contents) {
                QVERIFY2(!content.path.contains(QStringLiteral("..")),
                         qPrintable(QStringLiteral("%1: %2").arg(recipe.id, content.path)));
            }
        }
        for (const core::RecipeMoveStep& step : recipe.moves) {
            QVERIFY2(!step.file.contains(QStringLiteral("..")),
                     qPrintable(QStringLiteral("%1: %2").arg(recipe.id, step.file)));
            QVERIFY2(!step.target.contains(QStringLiteral("..")),
                     qPrintable(QStringLiteral("%1: %2").arg(recipe.id, step.target)));
            for (const core::RecipeRewriteRule& rule : step.rewrites) {
                QVERIFY2(!rule.filePattern.contains(QStringLiteral("..")),
                         qPrintable(QStringLiteral("%1: %2").arg(recipe.id, rule.filePattern)));
            }
        }
        // The rewrite rules were the ones this walk did not reach, and they
        // are the ones where a climbing path is worst: the file a rule names
        // is not merely read, it is edited and swapped in.
        for (const core::RecipeRewriteRule& rule : recipe.rewrites) {
            QVERIFY2(!rule.filePattern.contains(QStringLiteral("..")),
                     qPrintable(QStringLiteral("%1: %2").arg(recipe.id, rule.filePattern)));
        }
    }
}

void CatalogTest::everyStatePathStartsWithAKnownToken() {
    // A path that does not begin with a token Transmit knows resolves to
    // nothing, and the recipe then looks as though the application simply has
    // no state anywhere - which is indistinguishable from a typo.
    const core::RecipeCatalog catalog = builtIn();
    for (const core::AppRecipe& recipe : catalog.recipes()) {
        for (const core::RecipeStatePath& root : recipe.state) {
            for (auto it = root.candidatesByOs.constBegin(); it != root.candidatesByOs.constEnd();
                 ++it) {
                for (const QString& candidate : it.value()) {
                    const qsizetype close = candidate.indexOf(u'}');
                    QVERIFY2(candidate.startsWith(u'{') && close > 0,
                             qPrintable(QStringLiteral("%1: \"%2\" does not start with a token")
                                            .arg(recipe.id, candidate)));
                    const auto token =
                        format::tokenFromName(core::toUtf8(candidate.left(close + 1)));
                    QVERIFY2(token.hasValue(),
                             qPrintable(QStringLiteral("%1: \"%2\" is not a token Transmit knows")
                                            .arg(recipe.id, candidate.left(close + 1))));
                }
            }
        }
    }
}

/// A folder has one name here, and it is the most specific one.
///
/// On macOS "{HOME}/Library/Application Support/Firefox" and
/// "{APPCONFIG}/Firefox" are the same directory, so a recipe can be written
/// either way and look right. They are not the same to Transmit. A captured
/// file is filed under the longest known folder that contains it - always
/// {APPCONFIG} - while a restore looks for that application's state wherever
/// the recipe says, and a restore into a folder of the user's choosing gives
/// every token a directory of its own. The two then name different places:
/// the files land under APPCONFIG and the rewrite pass goes looking under
/// HOME, finds nothing, and every path inside that application's settings is
/// left pointing at the machine it came from. Nothing reports a problem.
///
/// The loader settles this for whatever it is given, so this reads the file
/// rather than the recipes: what ships should say what it means, and a
/// hundred and fifty entries that say one thing and mean another are a
/// hundred and fifty chances to reason from the wrong one.
///
/// Where two tokens name one directory - {APPCONFIG} and {APPDATA} are both
/// "Library/Application Support" on macOS - the one to write is the one
/// tokenising picks, because that is the one the files are filed under.
void CatalogTest::everyStatePathIsWrittenWithTheTokenItWouldBeReadAs() {
    struct Machine {
        format::OsFamily os;
        const char* home;
        QLatin1String name;
    };
    static const Machine kMachines[] = {
        {format::OsFamily::Windows, "C:/Users/bob", QLatin1String("windows")},
        {format::OsFamily::MacOs, "/Users/bob", QLatin1String("macos")},
        {format::OsFamily::Linux, "/home/bob", QLatin1String("linux")},
    };

    const QJsonArray entries = rawEntries(QStringLiteral(":/catalog/app-catalog.json"));
    QVERIFY2(entries.size() >= 150, "the built-in catalog is much smaller than it should be");

    QStringList wrong;
    for (const QJsonValue& value : entries) {
        const QJsonObject app = value.toObject();
        const QString id = app.value(QStringLiteral("id")).toString();

        for (const QJsonValue& stateValue : app.value(QStringLiteral("state")).toArray()) {
            const QJsonObject state = stateValue.toObject();
            const QJsonObject paths = state.value(QStringLiteral("paths")).toObject();

            for (const Machine& machine : kMachines) {
                const format::PathTokenMap folders =
                    format::PathTokenMap::defaultsFor(machine.os, machine.home);

                for (const QJsonValue& listed : paths.value(machine.name).toArray()) {
                    const QString candidate = listed.toString();
                    const QString resolved =
                        core::RecipeCatalog::resolveStatePath(candidate, folders);
                    if (resolved.isEmpty()) {
                        continue;  // a token this system has no folder for
                    }

                    const format::TokenizedPath read = folders.tokenize(core::toUtf8(resolved));
                    const std::string_view name = format::tokenName(read.token);
                    const QString wouldBeRead =
                        u'{' + QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())) +
                        u'}' +
                        (read.relative.empty() ? QString() : u'/' + core::fromUtf8(read.relative));
                    if (wouldBeRead != candidate) {
                        wrong << QStringLiteral("%1/%2 on %3: says \"%4\", read as \"%5\"")
                                     .arg(id, state.value(QStringLiteral("id")).toString(),
                                          machine.name, candidate, wouldBeRead);
                    }
                }
            }
        }
    }

    // All of them at once: this is a catalogue-wide shape, and finding it one
    // entry per run is not a way to fix two hundred and forty-nine of them.
    QVERIFY2(wrong.isEmpty(),
             qPrintable(QStringLiteral("%1 state paths name a folder by something other than the "
                                       "token it would be read as:\n  %2")
                            .arg(wrong.size())
                            .arg(wrong.mid(0, 15).join(QStringLiteral("\n  ")))));
}

/// And whatever it is handed is settled on the way in, because a user overlay
/// and a file written against the old schema are both outside this program's
/// reach to correct.
void CatalogTest::aFolderNamedTheLongWayRoundIsReadAsTheShortOne() {
    core::RecipeCatalog catalog;
    QCOMPARE(catalog.loadFromJson(R"([{
        "id": "test.long.way",
        "name": "The Long Way",
        "state": [{"id": "config", "paths": {
            "linux":   ["{HOME}/.config/lw", "{HOME}/.local/share/lw", "{HOME}/.lw"],
            "macos":   ["{HOME}/Library/Application Support/lw"],
            "windows": ["{HOME}/AppData/Roaming/lw"]
        }}]
    }])"),
             1);

    const core::RecipeStatePath* config =
        catalog.recipeById(QStringLiteral("test.long.way")).rootById(QStringLiteral("config"));
    QVERIFY(config != nullptr);

    QCOMPARE(config->candidatesByOs.value(QStringLiteral("linux")),
             QStringList({QStringLiteral("{APPCONFIG}/lw"), QStringLiteral("{APPDATA}/lw"),
                          QStringLiteral("{HOME}/.lw")}));
    QCOMPARE(config->candidatesByOs.value(QStringLiteral("macos")),
             QStringList({QStringLiteral("{APPCONFIG}/lw")}));
    QCOMPARE(config->candidatesByOs.value(QStringLiteral("windows")),
             QStringList({QStringLiteral("{APPCONFIG}/lw")}));
}

void CatalogTest::everyMoveStepNamesARootThatExists() {
    const core::RecipeCatalog catalog = builtIn();
    for (const core::AppRecipe& recipe : catalog.recipes()) {
        for (const core::RecipeMoveStep& step : recipe.moves) {
            if (step.rootId.isEmpty()) {
                continue;  // applies to every root
            }
            QVERIFY2(recipe.rootById(step.rootId) != nullptr,
                     qPrintable(QStringLiteral("%1: a move step names the root \"%2\", which the "
                                               "recipe does not have")
                                    .arg(recipe.id, step.rootId)));
        }
    }
}

void CatalogTest::everyMoveStepUsesAnActionWeImplement() {
    // moveActionFromName falls back to Copy for anything it does not know, so
    // a misspelt action would silently become "take it as it is" - which for a
    // step that meant "delete this or the application will not start" is the
    // worst possible substitution.
    const QSet<QString> known = {QStringLiteral("copy"),       QStringLiteral("skip"),
                                 QStringLiteral("rename"),     QStringLiteral("merge"),
                                 QStringLiteral("regenerate"), QStringLiteral("rewrite"),
                                 QStringLiteral("drop-keys")};

    for (const QJsonValue& value : rawEntries(QStringLiteral(":/catalog/app-catalog.json"))) {
        const QJsonObject entry = value.toObject();
        for (const QJsonValue& item : entry.value(QStringLiteral("move")).toArray()) {
            const QString action = item.toObject().value(QStringLiteral("action")).toString();
            if (action.isEmpty()) {
                continue;
            }
            QVERIFY2(known.contains(action),
                     qPrintable(QStringLiteral("%1: \"%2\" is not an action Transmit implements")
                                    .arg(entry.value(QStringLiteral("id")).toString(), action)));
        }
    }
}

void CatalogTest::noTwoApplicationsClaimTheSameFolder() {
    // Two recipes naming the same directory would capture it twice and, worse,
    // attribute it to whichever happened to be scanned first - so the report
    // would credit the wrong application and a per-application choice would
    // not do what it said.
    const core::RecipeCatalog catalog = builtIn();
    for (const char* system : {"windows", "macos", "linux"}) {
        QHash<QString, QString> claimedBy;
        for (const core::AppRecipe& recipe : catalog.recipes()) {
            for (const core::RecipeStatePath& root : recipe.state) {
                for (const QString& candidate :
                     root.candidatesByOs.value(QString::fromLatin1(system))) {
                    const QString normalised = candidate.toLower();
                    const QString owner = claimedBy.value(normalised);
                    QVERIFY2(
                        owner.isEmpty() || owner == recipe.id,
                        qPrintable(QStringLiteral("on %1, %2 and %3 both claim %4")
                                       .arg(QLatin1String(system), owner, recipe.id, candidate)));
                    claimedBy.insert(normalised, recipe.id);
                }
            }
        }
    }
}

void CatalogTest::carriesDataAgreesWithHavingState() {
    // The interface shows "this application's data can come with you" from
    // carries_data. If that said yes for an application with nowhere to read
    // from, the person would choose it and get nothing.
    const core::RecipeCatalog catalog = builtIn();
    for (const core::AppRecipe& recipe : catalog.recipes()) {
        if (recipe.portability.carriesData) {
            QVERIFY2(!recipe.state.isEmpty(),
                     qPrintable(QStringLiteral("%1 says its data travels but names nowhere it "
                                               "lives")
                                    .arg(recipe.id)));
        }
    }
}

void CatalogTest::everyRecipeCanBeFoundOneWayOrTheOther() {
    // Two ways an application is found: the system's list of installed
    // programs knows its name, or its state folder is simply there. A game
    // bought through a store is in nobody's list, so its folder is the only
    // evidence - and a recipe with neither is dead weight that can never
    // match anything.
    const core::RecipeCatalog catalog = builtIn();
    for (const core::AppRecipe& recipe : catalog.recipes()) {
        const bool named = !recipe.detectNames.isEmpty();
        const bool findable = !recipe.state.isEmpty();
        QVERIFY2(named || findable,
                 qPrintable(QStringLiteral("%1 has no detection names and no state, so nothing "
                                           "could ever match it")
                                .arg(recipe.id)));
    }
}

void CatalogTest::nothingInsideAStateFolderIsAnAbsolutePath() {
    // A content path is read relative to the state root it sits in. One that
    // began with a token or a root would be joined onto that folder and land
    // somewhere nobody meant - and, unlike "..", it does not look wrong.
    const core::RecipeCatalog catalog = builtIn();
    for (const core::AppRecipe& recipe : catalog.recipes()) {
        for (const core::RecipeStatePath& root : recipe.state) {
            for (const core::RecipeContent& content : root.contents) {
                const QString path = content.path;
                QVERIFY2(!path.startsWith(u'{'),
                         qPrintable(QStringLiteral("%1: \"%2\" is written as a known folder, but "
                                                   "it is read as a name inside %3")
                                        .arg(recipe.id, path, root.id)));
                QVERIFY2(
                    !path.startsWith(u'/') && !path.startsWith(u'\\'),
                    qPrintable(QStringLiteral("%1: \"%2\" starts at a root").arg(recipe.id, path)));
                QVERIFY2(
                    !path.contains(QStringLiteral(":/")) && !path.contains(QStringLiteral(":\\")),
                    qPrintable(QStringLiteral("%1: \"%2\" names a drive").arg(recipe.id, path)));
            }
        }
    }
}

void CatalogTest::noStateRootNamesTheSamePlaceTwice() {
    // Candidates are tried in order and the first that exists is taken, so a
    // repeat is never reached. It is always a copy-and-paste slip, and it
    // hides the candidate the author meant to write.
    const core::RecipeCatalog catalog = builtIn();
    for (const core::AppRecipe& recipe : catalog.recipes()) {
        for (const core::RecipeStatePath& root : recipe.state) {
            for (auto it = root.candidatesByOs.constBegin(); it != root.candidatesByOs.constEnd();
                 ++it) {
                QSet<QString> seen;
                for (const QString& candidate : it.value()) {
                    QVERIFY2(!seen.contains(candidate),
                             qPrintable(QStringLiteral("%1 names %2 twice for %3")
                                            .arg(recipe.id, candidate, it.key())));
                    seen.insert(candidate);
                }
            }
        }
    }
}

void CatalogTest::everySavedPasswordIsMarkedAsOne() {
    // Being marked sensitive is what keeps a file out of an archive nobody
    // asked to carry secrets in. The loader sets it for anything whose role is
    // "credentials"; this is the check that it kept doing so, because the
    // failure is silent and lands in somebody's unencrypted archive.
    const core::RecipeCatalog catalog = builtIn();
    int found = 0;
    for (const core::AppRecipe& recipe : catalog.recipes()) {
        for (const core::RecipeStatePath& root : recipe.state) {
            for (const core::RecipeContent& content : root.contents) {
                if (content.role != core::ContentRole::Credentials) {
                    continue;
                }
                ++found;
                QVERIFY2(content.sensitive,
                         qPrintable(QStringLiteral("%1: %2 holds credentials and is not marked "
                                                   "sensitive")
                                        .arg(recipe.id, content.path)));
            }
        }
    }
    QVERIFY2(found >= 20, "the catalog stopped describing credentials at all");
}

void CatalogTest::aSandboxedInstallIsFoundWhenItIsNotTheFirstCandidate() {
    // A recipe lists the native location first and the Flatpak one after it.
    // Somebody who has only the Flatpak has the second, and matching by state
    // has to look past the first to see it - which for most of a year it did
    // not do, so every sandboxed install was invisible unless the package
    // manager happened to name it too.
    QTemporaryDir home;
    QVERIFY(home.isValid());
    const QString sandboxed = home.filePath(QStringLiteral(".var/app/org.example.App/config/ex"));
    QVERIFY(QDir().mkpath(sandboxed));

    format::PathTokenMap folders;
    folders.setBase(format::PathTokenId::Home, home.path().toStdString());

    core::RecipeCatalog catalog;
    QCOMPARE(catalog.loadFromJson(R"({
      "schemaVersion": 2,
      "apps": [{
        "id": "org.example.app",
        "name": "Example",
        "state": [{
          "id": "config",
          "role": "config",
          "paths": { "linux": ["{HOME}/.config/ex", "{HOME}/.var/app/org.example.App/config/ex"] }
        }]
      }]
    })"),
             1);

    const QList<core::MatchedApp> found =
        catalog.matchByStateOnly({}, format::OsFamily::Linux, folders);
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.constFirst().recipe.id, QStringLiteral("org.example.app"));
    QVERIFY(found.constFirst().hasState);

    // And with neither present, nothing is claimed.
    QTemporaryDir empty;
    QVERIFY(empty.isValid());
    format::PathTokenMap nowhere;
    nowhere.setBase(format::PathTokenId::Home, empty.path().toStdString());
    QCOMPARE(catalog.matchByStateOnly({}, format::OsFamily::Linux, nowhere).size(), 0);
}

void CatalogTest::theOldSchemaAndTheNewProduceTheSameRecipes() {
    // tests/fixtures/app-catalog-v1.json is the catalog exactly as it was
    // before the migration. Everything it could express must still be read
    // identically, both because a user overlay may be written in that shape
    // and because it is the only evidence that the migration did not quietly
    // drop something from seventy-three entries.
    core::RecipeCatalog fromV1;
    QVERIFY(fromV1.loadFromFile(QStringLiteral(":/fixtures/app-catalog-v1.json")) >= 70);

    const core::RecipeCatalog fromV2 = builtIn();

    // The catalog has grown a long way past that file since. What it has to
    // show is that nothing the old one described went missing on the way, not
    // that the two are still the same size.
    QVERIFY2(fromV2.recipes().size() >= fromV1.recipes().size(),
             "the catalog is smaller than the file it was migrated from");

    for (const core::AppRecipe& old : fromV1.recipes()) {
        const core::AppRecipe fresh = fromV2.recipeById(old.id);
        QVERIFY2(fresh.isValid(), qPrintable(QStringLiteral("%1 vanished").arg(old.id)));

        QCOMPARE(fresh.displayName, old.displayName);
        QCOMPARE(fresh.detectNames, old.detectNames);
        QCOMPARE(fresh.installIds, old.installIds);
        QCOMPARE(fresh.quiesceProcesses, old.quiesceProcesses);
        QCOMPARE(fresh.expectedGrade, old.expectedGrade);
        QCOMPARE(fresh.note, old.note);
        QCOMPARE(fresh.state.size(), old.state.size());
        QCOMPARE(fresh.rewrites.size(), old.rewrites.size());

        for (qsizetype i = 0; i < old.state.size(); ++i) {
            const core::RecipeStatePath& before = old.state[i];
            const core::RecipeStatePath& after = fresh.state[i];
            QCOMPARE(after.role, before.role);
            QCOMPARE(after.excludePatterns, before.excludePatterns);

            // Version 1's single path is version 2's first candidate. Later
            // candidates are additions a person made deliberately, so only the
            // first is compared.
            for (const char* system : {"windows", "macos", "linux"}) {
                const QString key = QString::fromLatin1(system);
                const QStringList had = before.candidatesByOs.value(key);
                const QStringList has = after.candidatesByOs.value(key);
                QCOMPARE(has.isEmpty(), had.isEmpty());
                if (!had.isEmpty()) {
                    QCOMPARE(has.constFirst(), had.constFirst());
                }
            }
        }
    }
}

void CatalogTest::theInventoryPayloadSurvivesTheJourney() {
    // The archive carries the recipes the source machine used, so a target
    // with an older catalog still knows what to do. A field that encodes and
    // does not decode is silent: the restore simply behaves as though the
    // recipe never said anything.
    const core::RecipeCatalog catalog = builtIn();

    QList<core::MatchedApp> matched;
    for (const core::AppRecipe& recipe : catalog.recipes()) {
        core::MatchedApp match;
        match.recipe = recipe;
        match.installation.id = recipe.id;
        match.installation.displayName = recipe.displayName;
        matched.push_back(match);
    }
    QVERIFY(matched.size() >= 150);

    const format::ByteBuffer encoded = core::encodeAppInventory(matched);
    const QList<core::InventoryEntry> decoded = core::decodeAppInventory(encoded);
    QCOMPARE(decoded.size(), matched.size());

    for (qsizetype i = 0; i < matched.size(); ++i) {
        const core::AppRecipe& sent = matched[i].recipe;
        const core::AppRecipe back = decoded[i].toRecipe();

        QCOMPARE(back.id, sent.id);
        QCOMPARE(back.displayName, sent.displayName);
        QCOMPARE(back.installIds, sent.installIds);
        QCOMPARE(back.expectedGrade, sent.expectedGrade);
        QCOMPARE(back.note, sent.note);
        QCOMPARE(back.portability.carriesData, sent.portability.carriesData);
        QCOMPARE(back.state.size(), sent.state.size());
        QCOMPARE(back.moves.size(), sent.moves.size());
        QCOMPARE(back.rewrites.size(), sent.rewrites.size());

        for (qsizetype r = 0; r < sent.state.size(); ++r) {
            QCOMPARE(back.state[r].id, sent.state[r].id);
            QCOMPARE(back.state[r].role, sent.state[r].role);
            QCOMPARE(back.state[r].excludePatterns, sent.state[r].excludePatterns);
            QCOMPARE(back.state[r].candidatesByOs, sent.state[r].candidatesByOs);
            QCOMPARE(back.state[r].contents.size(), sent.state[r].contents.size());
            for (qsizetype c = 0; c < sent.state[r].contents.size(); ++c) {
                QCOMPARE(back.state[r].contents[c].path, sent.state[r].contents[c].path);
                QCOMPARE(back.state[r].contents[c].role, sent.state[r].contents[c].role);
                QCOMPARE(back.state[r].contents[c].portable, sent.state[r].contents[c].portable);
                QCOMPARE(back.state[r].contents[c].sensitive, sent.state[r].contents[c].sensitive);
                QCOMPARE(back.state[r].contents[c].live, sent.state[r].contents[c].live);
            }
        }
        for (qsizetype m = 0; m < sent.moves.size(); ++m) {
            QCOMPARE(back.moves[m].fromOs, sent.moves[m].fromOs);
            QCOMPARE(back.moves[m].toOs, sent.moves[m].toOs);
            QCOMPARE(back.moves[m].rootId, sent.moves[m].rootId);
            QCOMPARE(back.moves[m].file, sent.moves[m].file);
            QCOMPARE(back.moves[m].action, sent.moves[m].action);
            QCOMPARE(back.moves[m].keys, sent.moves[m].keys);
        }
    }
}

/// A grade and the sentence explaining it have to be about the same journey.
///
/// The wildcard exists so a recipe can say "adapted everywhere, except to
/// Windows, where it is manual" without writing out every other pair - and the
/// grade honoured that while the reason did not: it was the first row that
/// matched at all and carried a sentence, so the person was shown the manual
/// grade beside the explanation of the adapted one. Nothing in the shipped
/// catalogue is written that way yet, which is precisely why this is stated
/// here rather than waiting to be noticed by whoever writes the first one.
/// An archive carries the recipes the capture used, and a restore believes
/// them over the catalogue it has - which is right, since the archive knows
/// what was actually taken. So a capture by a build that named these folders
/// the long way round would hand a restore a directory its files were never
/// put in, and the rewrite pass would find nothing there and say nothing
/// about it. That reaches the restore through the archive rather than
/// through a file, so it is settled on the way out as well.
void CatalogTest::anArchiveThatNamesAFolderTheLongWayRoundIsReadTheShortWay() {
    core::MatchedApp match;
    match.recipe.id = QStringLiteral("test.old.capture");
    match.recipe.displayName = QStringLiteral("An Older Capture");
    match.installation.id = match.recipe.id;

    core::RecipeStatePath state;
    state.id = QStringLiteral("config");
    state.role = QStringLiteral("config");

    // Written straight onto the structure, which is what a build without the
    // settling did and what its archives therefore hold.
    state.candidatesByOs.insert(QStringLiteral("macos"),
                                {QStringLiteral("{HOME}/Library/Application Support/OldCapture")});
    state.candidatesByOs.insert(QStringLiteral("linux"),
                                {QStringLiteral("{HOME}/.config/oldcapture")});
    match.recipe.state.push_back(state);

    const core::AppRecipe back =
        core::decodeAppInventory(core::encodeAppInventory({match})).first().toRecipe();
    QCOMPARE(back.state.size(), qsizetype(1));
    QCOMPARE(back.state[0].candidatesByOs.value(QStringLiteral("macos")),
             QStringList({QStringLiteral("{APPCONFIG}/OldCapture")}));
    QCOMPARE(back.state[0].candidatesByOs.value(QStringLiteral("linux")),
             QStringList({QStringLiteral("{APPCONFIG}/oldcapture")}));
}

void CatalogTest::theGradeAndTheReasonComeFromTheSameEntry() {
    using format::OsFamily;

    core::RecipePortability portability;
    portability.pairs.push_back(core::RecipePortability::Pair{
        OsFamily::Unknown, OsFamily::Unknown, core::ContinuityGrade::Adapted,
        QStringLiteral("the paths inside it are corrected on the way in")});
    portability.pairs.push_back(core::RecipePortability::Pair{
        OsFamily::Unknown, OsFamily::Windows, core::ContinuityGrade::Manual,
        QStringLiteral("there is no Windows build, so it has to be set up by hand")});

    QCOMPARE(portability.gradeFor(OsFamily::Linux, OsFamily::MacOs, core::ContinuityGrade::Full),
             core::ContinuityGrade::Adapted);
    QCOMPARE(portability.reasonFor(OsFamily::Linux, OsFamily::MacOs),
             QStringLiteral("the paths inside it are corrected on the way in"));

    QCOMPARE(portability.gradeFor(OsFamily::Linux, OsFamily::Windows, core::ContinuityGrade::Full),
             core::ContinuityGrade::Manual);
    QCOMPARE(portability.reasonFor(OsFamily::Linux, OsFamily::Windows),
             QStringLiteral("there is no Windows build, so it has to be set up by hand"));

    // A journey the catalogue says nothing about has no reason to give, and
    // the grade falls back rather than being invented.
    core::RecipePortability narrow;
    narrow.pairs.push_back(core::RecipePortability::Pair{OsFamily::MacOs, OsFamily::Linux,
                                                         core::ContinuityGrade::Manual,
                                                         QStringLiteral("only that way round")});
    QCOMPARE(narrow.gradeFor(OsFamily::Linux, OsFamily::MacOs, core::ContinuityGrade::Full),
             core::ContinuityGrade::Full);
    QCOMPARE(narrow.reasonFor(OsFamily::Linux, OsFamily::MacOs), QString());

    // And an entry that gives a grade without a sentence is not a reason to
    // reach for somebody else's sentence.
    core::RecipePortability silent;
    silent.pairs.push_back(core::RecipePortability::Pair{OsFamily::Unknown, OsFamily::Unknown,
                                                         core::ContinuityGrade::Adapted,
                                                         QStringLiteral("the general case")});
    silent.pairs.push_back(core::RecipePortability::Pair{OsFamily::Unknown, OsFamily::Windows,
                                                         core::ContinuityGrade::Manual, QString()});
    QCOMPARE(silent.reasonFor(OsFamily::Linux, OsFamily::Windows), QString());
}

QTEST_MAIN(CatalogTest)
#include "CatalogTest.moc"
