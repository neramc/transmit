// Choosing which applications travel, and how much of a folder comes with them.
//
// Two promises are made on the capture page and both are easy to break without
// anyone noticing until the archive is on the other machine: an application
// marked "data travels" must actually have its data taken, and an application
// hidden behind a filter must still be written down as installed. Nothing in
// the interface tells you which of those went wrong - the report on the far
// side does, weeks later.

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "app/ExportController.h"
#include "app/models/AppCatalogModel.h"
#include "core/continuity/ContinuityTypes.h"
#include "core/recipe/RecipeCatalog.h"
#include "core/services/ExportService.h"
#include "platform/PlatformService.h"

using namespace transmit;

class AppSelectionTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void theListSeparatesWhatTravelsFromWhatIsOnlyNoted();
    void theFilterHidesRowsWithoutForgettingThem();
    void clearingWhatIsShownLeavesTheRestAlone();
    void everyApplicationIsRecordedForTheReinstallScript();
    void nothingCarriesDataItHasNotGot();
    void closingIsOnlyAskedForTheApplicationsBeingTaken();
    void theFileLimitsAreReadBackInWords();
    void anImpossibleSizeLimitIsNoLimitAtAll();
    void theApplicationSummarySaysWhichCaseThisIs();

private:
    /// Fills the model and waits for the worker to answer.
    void load(app::AppCatalogModel& model);

    /// Creates the state folders the catalog declares for this system.
    void plantState(const platform::PlatformService& platform);

    /// The same for one named application. False when the catalog puts its
    /// state somewhere outside the fake home on this system, which is not a
    /// failure - it is a test that cannot run here.
    bool plantStateFor(const platform::PlatformService& platform, const QString& appId);
    bool plantStateOf(const core::AppRecipe& recipe, const format::PathTokenMap& folders,
                      format::OsFamily os);

    /// The row showing this application, or -1.
    [[nodiscard]] static int rowOf(const app::AppCatalogModel& model, const QString& appId);

    [[nodiscard]] static QVariant at(const app::AppCatalogModel& model, int row, int role);

    std::unique_ptr<QTemporaryDir> workspace_;
    QString home_;

    /// True when this platform resolves its known folders from the environment,
    /// so the fake home below is the one the detection actually reads.
    bool homeIsHonoured_ = false;

    /// The application ids whose state folders the fixture created, taken from
    /// the catalog so they are right on whichever system this is.
    QStringList planted_;
};

void AppSelectionTest::initTestCase() {
    workspace_ = std::make_unique<QTemporaryDir>();
    QVERIFY(workspace_->isValid());

    home_ = workspace_->filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(home_ + QStringLiteral("/.config")));

    qputenv("HOME", home_.toUtf8());
    qputenv("XDG_CONFIG_HOME", (home_ + QStringLiteral("/.config")).toUtf8());

    // Windows and macOS answer the known-folder question through their own
    // shell APIs, which do not follow HOME. Rather than read whatever is in
    // the runner's real home directory - and produce a result that depends on
    // what happens to be installed there - the tests that need a known home
    // say so and skip.
    const auto platform = platform::PlatformService::create();
    const auto base = platform->knownFolders().base(format::PathTokenId::Home);
    homeIsHonoured_ = base && QString::fromStdString(*base) == home_;
    if (!homeIsHonoured_) {
        return;
    }

    plantState(*platform);
    QVERIFY2(!planted_.isEmpty(),
             "the catalog declares no state folder inside the home directory on this system");
}

void AppSelectionTest::plantState(const platform::PlatformService& platform) {
    // A home directory with a few applications' state in it, so the detection
    // has something to find on a machine that may have nothing installed at
    // all.
    //
    // The paths come from the catalog rather than being written out here: the
    // same application keeps its settings somewhere different on each system,
    // and a fixture that plants ~/.mozilla/firefox tests nothing on macOS,
    // where Firefox has never put a profile there. Asking the catalog is also
    // the only version of this that cannot rot when a recipe is corrected.
    core::RecipeCatalog catalog;
    catalog.loadDefaults();

    const format::PathTokenMap folders = platform.knownFolders();
    const format::OsFamily os = platform.environment().os;

    for (const core::AppRecipe& recipe : catalog.recipes()) {
        if (!recipe.portability.carriesData) {
            continue;
        }
        if (plantStateOf(recipe, folders, os)) {
            planted_.push_back(recipe.id);
        }
        if (planted_.size() >= 4) {
            break;
        }
    }
}

bool AppSelectionTest::plantStateOf(const core::AppRecipe& recipe,
                                    const format::PathTokenMap& folders, format::OsFamily os) {
    for (const core::RecipeStatePath& state : recipe.state) {
        const QStringList candidates = state.candidatesForOs(os);
        if (candidates.isEmpty()) {
            continue;
        }
        const QString resolved =
            core::RecipeCatalog::resolveStatePath(candidates.constFirst(), folders);
        // Only inside the fake home: creating anything anywhere else would be
        // writing into whoever is running the tests.
        if (resolved.isEmpty() || !resolved.startsWith(home_)) {
            continue;
        }

        // Made as a directory with a marker inside, whether the real thing is
        // a file or a folder. What is being tested is that Transmit notices
        // the state is here, and existence is the whole of that question.
        if (!QDir().mkpath(resolved)) {
            continue;
        }
        QFile marker(resolved + QStringLiteral("/planted-by-the-test"));
        if (!marker.open(QIODevice::WriteOnly)) {
            continue;
        }
        marker.write("so the folder is not empty\n");
        return true;
    }
    return false;
}

bool AppSelectionTest::plantStateFor(const platform::PlatformService& platform,
                                     const QString& appId) {
    core::RecipeCatalog catalog;
    catalog.loadDefaults();

    const core::AppRecipe recipe = catalog.recipeById(appId);
    if (!recipe.isValid()) {
        return false;
    }
    return plantStateOf(recipe, platform.knownFolders(), platform.environment().os);
}

void AppSelectionTest::cleanupTestCase() {
    workspace_.reset();
}

void AppSelectionTest::load(app::AppCatalogModel& model) {
    QSignalSpy loaded(&model, &app::AppCatalogModel::countsChanged);
    model.refresh();
    // Asking the system what is installed shells out to a package manager, so
    // this is generous rather than tight.
    QVERIFY2(loaded.wait(120000), "the detection never answered");
    QVERIFY(!model.isLoading());
}

int AppSelectionTest::rowOf(const app::AppCatalogModel& model, const QString& appId) {
    for (int row = 0; row < model.rowCount(); ++row) {
        if (at(model, row, app::AppCatalogModel::AppIdRole).toString() == appId) {
            return row;
        }
    }
    return -1;
}

QVariant AppSelectionTest::at(const app::AppCatalogModel& model, int row, int role) {
    return model.data(model.index(row), role);
}

void AppSelectionTest::theListSeparatesWhatTravelsFromWhatIsOnlyNoted() {
    if (!homeIsHonoured_) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

    app::AppCatalogModel model;
    load(model);

    QVERIFY2(model.totalCount() > 0, "nothing at all was recognised");
    QVERIFY2(model.carriesDataCount() > 0,
             "state folders were planted in the fake home and none was found");

    // Filtered to the ones worth deciding about, which is what the page shows
    // on arrival.
    QVERIFY(model.carriesDataOnly());
    QVERIFY(model.rowCount() <= model.totalCount());
    for (int row = 0; row < model.rowCount(); ++row) {
        QVERIFY2(at(model, row, app::AppCatalogModel::CarriesDataRole).toBool(),
                 qPrintable(at(model, row, app::AppCatalogModel::AppIdRole).toString()));
    }

    for (const QString& appId : planted_) {
        QVERIFY2(rowOf(model, appId) >= 0,
                 qPrintable(
                     QStringLiteral("%1 has state in the fake home and was not found").arg(appId)));
    }

    // And everything chosen to start with, because there is no reason to have
    // found it otherwise.
    QCOMPARE(model.selectedCount(), model.carriesDataCount());
}

void AppSelectionTest::theFilterHidesRowsWithoutForgettingThem() {
    if (!homeIsHonoured_) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

    app::AppCatalogModel model;
    load(model);

    const int all = model.totalCount();
    const int carrying = model.rowCount();

    model.setCarriesDataOnly(false);
    QCOMPARE(model.rowCount(), all);
    QVERIFY2(model.rowCount() >= carrying, "showing more hid something");

    // An id, which is unique, so this matches exactly the one row.
    model.setFilterText(planted_.constFirst());
    QVERIFY2(model.rowCount() > 0, qPrintable(QStringLiteral("%1 is in the list and the search "
                                                             "did not find it")
                                                  .arg(planted_.constFirst())));
    QVERIFY2(model.rowCount() < all, "the search matched everything");
    QCOMPARE(model.totalCount(), all);

    // The answer handed to the capture covers every application, not the four
    // the search happens to be showing.
    QCOMPARE(model.selection().size(), all);

    model.setFilterText(QString());
    QCOMPARE(model.rowCount(), all);
}

void AppSelectionTest::clearingWhatIsShownLeavesTheRestAlone() {
    if (!homeIsHonoured_) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

    app::AppCatalogModel model;
    load(model);
    const int chosenToStart = model.selectedCount();
    QVERIFY2(chosenToStart > 1, "this needs more than one application to be meaningful");

    model.setFilterText(planted_.constFirst());
    const int showing = model.rowCount();
    QVERIFY(showing >= 1);
    QVERIFY(showing < chosenToStart);

    // "Select none" while a search is running means the rows on screen. The
    // alternative - quietly clearing sixty rows nobody can see - is the sort of
    // thing only discovered on the other machine.
    model.selectAll(false);
    QCOMPARE(model.selectedCount(), chosenToStart - showing);

    model.setFilterText(QString());
    QCOMPARE(model.selectedCount(), chosenToStart - showing);
}

void AppSelectionTest::everyApplicationIsRecordedForTheReinstallScript() {
    if (!homeIsHonoured_) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

    app::AppCatalogModel model;
    load(model);

    // The filter left where the page puts it, so most of the list is hidden.
    // An answer that only covered what was on screen would quietly drop every
    // hidden application out of the inventory, and the restore on the far side
    // would never offer to install them.
    QVERIFY2(model.rowCount() < model.totalCount(), "nothing is being hidden to test with");
    model.selectAll(false);

    const QList<core::AppSelection> answers = model.selection();
    QCOMPARE(answers.size(), model.totalCount());
    for (const core::AppSelection& answer : answers) {
        QVERIFY2(answer.recordForReinstall,
                 qPrintable(QStringLiteral("%1 was dropped from the inventory by being "
                                           "deselected")
                                .arg(answer.appId)));
        QVERIFY2(!answer.captureState, qPrintable(answer.appId));
    }
}

void AppSelectionTest::nothingCarriesDataItHasNotGot() {
    if (!homeIsHonoured_) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

    app::AppCatalogModel model;
    load(model);
    model.setCarriesDataOnly(false);

    // Tick every row, including the ones that are only being noted. The answer
    // must still say no for those: a tick beside an application whose folders
    // are not on this machine would promise something Transmit cannot do.
    model.selectAll(true);

    QHash<QString, bool> travels;
    for (int row = 0; row < model.rowCount(); ++row) {
        travels.insert(at(model, row, app::AppCatalogModel::AppIdRole).toString(),
                       at(model, row, app::AppCatalogModel::CarriesDataRole).toBool());
    }

    int promised = 0;
    for (const core::AppSelection& answer : model.selection()) {
        QCOMPARE(answer.captureState, travels.value(answer.appId));
        promised += answer.captureState ? 1 : 0;
    }
    QCOMPARE(promised, model.carriesDataCount());
}

void AppSelectionTest::closingIsOnlyAskedForTheApplicationsBeingTaken() {
    if (!homeIsHonoured_) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }
#ifndef Q_OS_LINUX
    QSKIP("the process name a running program reports is read differently on each platform");
#else
    const auto platform = platform::PlatformService::create();

    // Firefox has to be one of the applications the machine appears to have,
    // or its quiesce names are never collected and this would be measuring
    // nothing.
    if (!plantStateFor(*platform, QStringLiteral("org.mozilla.firefox"))) {
        QSKIP("the catalog puts Firefox's state outside the fake home on this system");
    }

    // A process that reports itself as "firefox", which is one of the names the
    // Firefox recipe asks to be closed. Copied rather than started under a
    // shell: the name the check reads is the executable's, not the script's.
    const QString pretend = workspace_->filePath(QStringLiteral("firefox"));
    QVERIFY(QFile::copy(QStringLiteral("/bin/sleep"), pretend));
    QVERIFY(QFile::setPermissions(pretend, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

    QProcess sleeper;
    sleeper.start(pretend, {QStringLiteral("30")});
    QVERIFY2(sleeper.waitForStarted(5000), qPrintable(sleeper.errorString()));

    const core::ExportService service(*platform);

    core::CaptureSelection everything;
    everything.domains.insert(static_cast<int>(format::DomainId::AppState));

    const QList<platform::RunningApp> asked = service.applicationsToClose(everything);
    const auto namesFirefox = [](const QList<platform::RunningApp>& apps) {
        for (const platform::RunningApp& app : apps) {
            if (app.processName.contains(QStringLiteral("firefox"), Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    };
    QVERIFY2(namesFirefox(asked),
             "a process called firefox is running and the check did not mention it");

    // The same machine, the same running process - but its data was not
    // chosen, so there is nothing to be gained by making somebody close it.
    core::CaptureSelection withoutFirefox = everything;
    withoutFirefox.appMode = core::AppSelectionMode::Explicit;
    core::AppSelection firefox;
    firefox.appId = QStringLiteral("org.mozilla.firefox");
    firefox.captureState = false;
    withoutFirefox.apps.push_back(firefox);

    QVERIFY2(!namesFirefox(service.applicationsToClose(withoutFirefox)),
             "an application whose data is not being taken was still to be closed");

    sleeper.kill();
    sleeper.waitForFinished(5000);
#endif
}

void AppSelectionTest::theFileLimitsAreReadBackInWords() {
    app::ExportController controller;
    QCOMPARE(controller.scopeSummary(), QStringLiteral("Everything in the folders you chose"));

    controller.setScope(1073741824.0, 30, QStringLiteral("iso, VMDK .dmg"));

    const QString summary = controller.scopeSummary();
    QVERIFY2(summary.contains(QStringLiteral("1.00 GiB")), qPrintable(summary));

    // Written out rather than left as Qt's untranslated plural form: "30
    // day(s)" is what a program says, not what a person reading the page does.
    QVERIFY2(summary.contains(QStringLiteral("nothing untouched for 30 days")),
             qPrintable(summary));

    // Sorted, lowercased, and the punctuation the user typed thrown away -
    // otherwise ".dmg" and "dmg" are two different kinds of file.
    QVERIFY2(summary.contains(QStringLiteral("no .dmg, .iso, .vmdk files")), qPrintable(summary));

    controller.clearScope();
    QCOMPARE(controller.scopeSummary(), QStringLiteral("Everything in the folders you chose"));
}

void AppSelectionTest::anImpossibleSizeLimitIsNoLimitAtAll() {
    app::ExportController controller;

    // A negative or non-finite limit cast straight to an unsigned type becomes
    // an enormous one, which reads as "no limit" - the opposite of what
    // dragging a size control down to nothing means.
    for (const double impossible :
         {-1.0, -1e18, 0.5, std::nan(""), -std::numeric_limits<double>::infinity()}) {
        controller.setScope(impossible, 0, QString());
        QCOMPARE(controller.scopeSummary(), QStringLiteral("Everything in the folders you chose"));
    }

    controller.setScope(std::numeric_limits<double>::infinity(), 0, QString());
    QCOMPARE(controller.scopeSummary(), QStringLiteral("Everything in the folders you chose"));
}

void AppSelectionTest::theApplicationSummarySaysWhichCaseThisIs() {
    if (!homeIsHonoured_) {
        QSKIP("this platform does not resolve its known folders from HOME");
    }

    app::ExportController controller;
    QCOMPARE(controller.applicationSummary(),
             QStringLiteral("Every program whose data can travel"));

    app::AppCatalogModel model;
    load(model);

    controller.chooseApplications(&model);
    const QString everything = controller.applicationSummary();
    QVERIFY2(everything.contains(QStringLiteral("whose data can travel")) &&
                 !everything.contains(QStringLiteral("(s)")),
             qPrintable(everything));

    model.selectAll(false);
    controller.chooseApplications(&model);
    QCOMPARE(controller.applicationSummary(),
             QStringLiteral("No program data - only the list of what you had installed"));

    model.setSelected(0, true);
    controller.chooseApplications(&model);
    const QString some = controller.applicationSummary();
    QVERIFY2(some.startsWith(QStringLiteral("1 of ")), qPrintable(some));

    controller.carryEveryApplication();
    QCOMPARE(controller.applicationSummary(),
             QStringLiteral("Every program whose data can travel"));
}

QTEST_MAIN(AppSelectionTest)
#include "AppSelectionTest.moc"
