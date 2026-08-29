// The application catalog: what it says, and whether it is internally sound.
//
// The catalog is the only part of Transmit whose content is data rather than
// code, and the only part a user can extend without building anything. Both of
// those mean it has to be checked as data: a recipe naming a state root that
// does not exist, or a path that climbs out of the folder it names, is not a
// compile error and would never be one.

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
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
    void everyMoveStepNamesARootThatExists();
    void everyMoveStepUsesAnActionWeImplement();
    void noTwoApplicationsClaimTheSameFolder();
    void carriesDataAgreesWithHavingState();
    void theOldSchemaAndTheNewProduceTheSameRecipes();
    void theInventoryPayloadSurvivesTheJourney();

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
    QVERIFY2(entries.size() >= 70, "the built-in catalog is much smaller than it should be");

    QSet<QString> seen;
    for (const QJsonValue& value : entries) {
        const QString id = value.toObject().value(QStringLiteral("id")).toString();
        QVERIFY2(!id.isEmpty(), "a recipe has no id");

        // A duplicate is not caught by the loader: the second silently replaces
        // the first, which is exactly what a user overlay is meant to do and
        // exactly what the built-in file must never do to itself.
        QVERIFY2(!seen.contains(id), qPrintable(QStringLiteral("two recipes share the id %1").arg(id)));
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
    const QSet<QString> known = {
        QStringLiteral("copy"),   QStringLiteral("skip"),      QStringLiteral("rename"),
        QStringLiteral("merge"),  QStringLiteral("regenerate"), QStringLiteral("rewrite"),
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
                    QVERIFY2(owner.isEmpty() || owner == recipe.id,
                             qPrintable(QStringLiteral("on %1, %2 and %3 both claim %4")
                                            .arg(QLatin1String(system), owner, recipe.id,
                                                 candidate)));
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

void CatalogTest::theOldSchemaAndTheNewProduceTheSameRecipes() {
    // tests/fixtures/app-catalog-v1.json is the catalog exactly as it was
    // before the migration. Everything it could express must still be read
    // identically, both because a user overlay may be written in that shape
    // and because it is the only evidence that the migration did not quietly
    // drop something from seventy-three entries.
    core::RecipeCatalog fromV1;
    QVERIFY(fromV1.loadFromFile(QStringLiteral(":/fixtures/app-catalog-v1.json")) >= 70);

    const core::RecipeCatalog fromV2 = builtIn();
    QCOMPARE(fromV1.recipes().size(), fromV2.recipes().size());

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
    QVERIFY(matched.size() >= 70);

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
                QCOMPARE(back.state[r].contents[c].sensitive,
                         sent.state[r].contents[c].sensitive);
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

QTEST_MAIN(CatalogTest)
#include "CatalogTest.moc"
