#include <QGuiApplication>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

namespace {

/// Every QML warning the engine emits while a test is running. A binding that
/// resolves to undefined is a bug the interface will happily paint around, so
/// the only way to keep the tree honest is to treat warnings as failures.
QStringList g_messages;
QStringList g_loadMessages;
QtMessageHandler g_previous = nullptr;

void collectMessages(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        g_messages.append(message);
    }
    if (g_previous != nullptr) {
        g_previous(type, context, message);
    }
}

}  // namespace

/// Loads the real window and drives it, which is the only way to find out
/// whether the QML actually resolves: the compiler is happy with a binding to
/// a property that does not exist.
class UiSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void theWindowLoadsWithoutWarnings();
    void onlyTheOpeningPageIsBuiltAtStartup();
    void visitingAPageBuildsItOnceAndKeepsIt();
    void theThemeFollowsTheController();
    void reducedMotionSilencesTheAnimations();
    void confirmingADialogRunsTheAction();
    void dismissingADialogRunsNothing();

private:
    /// Lets the engine finish the asynchronous loaders and the animations.
    static void settle(int milliseconds = 250);

    [[nodiscard]] QQuickWindow* window() const;
    [[nodiscard]] QObject* named(const char* objectName) const;
    [[nodiscard]] QStringList loadedPages() const;

    /// Runs an expression in the shell's own scope. That scope imports every
    /// module the interface uses, so the theme singletons, the controllers and
    /// the dialog host are all reachable by the names the QML itself uses -
    /// which is a truer test than reaching around the engine for them.
    QVariant evaluate(const QString& expression) const;

    std::unique_ptr<QQmlApplicationEngine> engine_;
};

void UiSmokeTest::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    g_previous = qInstallMessageHandler(collectMessages);

    engine_ = std::make_unique<QQmlApplicationEngine>();
    engine_->addImportPath(QStringLiteral(":/qt/qml"));
    engine_->load(QUrl(QStringLiteral("qrc:/qt/qml/Transmit/Main.qml")));

    QVERIFY2(!engine_->rootObjects().isEmpty(), "the main window did not load");
    settle();

    // Kept apart from the running tally: init() clears that before each test,
    // and what the first load said is the most interesting thing here.
    g_loadMessages = g_messages;
}

void UiSmokeTest::cleanupTestCase() {
    engine_.reset();
    qInstallMessageHandler(g_previous);
}

void UiSmokeTest::init() {
    g_messages.clear();
}

void UiSmokeTest::settle(int milliseconds) {
    QTest::qWait(milliseconds);
}

QQuickWindow* UiSmokeTest::window() const {
    return engine_->rootObjects().isEmpty()
               ? nullptr
               : qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst());
}

QObject* UiSmokeTest::named(const char* objectName) const {
    return engine_->rootObjects().isEmpty()
               ? nullptr
               : engine_->rootObjects().constFirst()->findChild<QObject*>(
                     QString::fromLatin1(objectName));
}

QStringList UiSmokeTest::loadedPages() const {
    QObject* const content = named("contentView");
    return content == nullptr ? QStringList() : content->property("loadedPages").toStringList();
}

QVariant UiSmokeTest::evaluate(const QString& expression) const {
    QObject* const shell = named("appShell");
    if (shell == nullptr) {
        return {};
    }
    QQmlExpression evaluated(qmlContext(shell), shell, expression);
    const QVariant result = evaluated.evaluate();
    if (evaluated.hasError()) {
        g_messages.append(evaluated.error().toString());
    }
    return result;
}

void UiSmokeTest::theWindowLoadsWithoutWarnings() {
    QVERIFY(window() != nullptr);
    QVERIFY2(g_loadMessages.isEmpty(), qPrintable(g_loadMessages.join(u'\n')));
    QCOMPARE(window()->property("title").toString(), QStringLiteral("Transmit"));
    QVERIFY(named("contentView") != nullptr);
    QVERIFY(named("dialogHost") != nullptr);
}

void UiSmokeTest::onlyTheOpeningPageIsBuiltAtStartup() {
    // Building all five pages up front is the difference between a window that
    // appears at once and one that appears eventually.
    QCOMPARE(loadedPages(), QStringList{QStringLiteral("home")});
}

void UiSmokeTest::visitingAPageBuildsItOnceAndKeepsIt() {
    const QStringList pages{QStringLiteral("export"), QStringLiteral("import"),
                            QStringLiteral("report"), QStringLiteral("settings")};
    for (const QString& page : pages) {
        evaluate(QStringLiteral("AppController.currentPage = '%1'").arg(page));
        settle();
        QVERIFY2(loadedPages().contains(page), qPrintable(QStringLiteral("%1 never loaded").arg(page)));
        QVERIFY2(g_messages.isEmpty(),
                 qPrintable(QStringLiteral("%1 warned: %2").arg(page, g_messages.join(u'\n'))));
    }

    QCOMPARE(loadedPages().size(), 5);

    // Going back must not rebuild: a half-filled wizard has to still be there.
    evaluate(QStringLiteral("AppController.currentPage = 'export'"));
    settle();
    QCOMPARE(loadedPages().size(), 5);

    evaluate(QStringLiteral("AppController.currentPage = 'home'"));
    settle();
    QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join(u'\n')));
}

void UiSmokeTest::theThemeFollowsTheController() {
    evaluate(QStringLiteral("AppController.themeMode = 'dark'"));
    settle(50);
    QVERIFY(evaluate(QStringLiteral("Colors.dark")).toBool());
    const QColor darkBackground = evaluate(QStringLiteral("Colors.background")).value<QColor>();

    evaluate(QStringLiteral("AppController.themeMode = 'light'"));
    settle(50);
    QVERIFY(!evaluate(QStringLiteral("Colors.dark")).toBool());
    const QColor lightBackground = evaluate(QStringLiteral("Colors.background")).value<QColor>();

    QVERIFY(darkBackground.isValid());
    QVERIFY(lightBackground.isValid());
    QVERIFY2(darkBackground != lightBackground, "the two schemes paint the same background");
    QVERIFY(lightBackground.lightnessF() > darkBackground.lightnessF());

    evaluate(QStringLiteral("AppController.themeMode = 'system'"));
    settle(50);
    QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join(u'\n')));
}

void UiSmokeTest::reducedMotionSilencesTheAnimations() {
    evaluate(QStringLiteral("AppController.reduceMotion = true"));
    settle(50);
    QVERIFY(evaluate(QStringLiteral("Motion.reduced")).toBool());
    QCOMPARE(evaluate(QStringLiteral("Motion.duration(200)")).toInt(), 0);

    evaluate(QStringLiteral("AppController.reduceMotion = false"));
    settle(50);
    QVERIFY(!evaluate(QStringLiteral("Motion.reduced")).toBool());
    QCOMPARE(evaluate(QStringLiteral("Motion.duration(200)")).toInt(), 200);
    QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join(u'\n')));
}

void UiSmokeTest::confirmingADialogRunsTheAction() {
    // The callback moves the application somewhere observable rather than
    // setting a flag: what matters is that the action really ran, and a real
    // property proves that without the interface growing a hook for the test.
    evaluate(QStringLiteral("AppController.currentPage = 'home'"));
    evaluate(QStringLiteral(
        "dialogs.confirm({ heading: 'Restore?', body: 'This writes files.', destructive: true },"
        "                function () { AppController.currentPage = 'report' })"));
    settle(400);

    QObject* const dialog = named("confirmDialog");
    QVERIFY(dialog != nullptr);
    QVERIFY2(dialog->property("opened").toBool(), "the dialog did not open");
    QCOMPARE(dialog->property("heading").toString(), QStringLiteral("Restore?"));
    QVERIFY(dialog->property("destructive").toBool());
    QCOMPARE(evaluate(QStringLiteral("AppController.currentPage")).toString(),
             QStringLiteral("home"));

    QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
    settle(400);

    QCOMPARE(evaluate(QStringLiteral("AppController.currentPage")).toString(),
             QStringLiteral("report"));
    QVERIFY(!dialog->property("opened").toBool());
    QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join(u'\n')));
}

void UiSmokeTest::dismissingADialogRunsNothing() {
    evaluate(QStringLiteral("AppController.currentPage = 'home'"));
    evaluate(QStringLiteral("dialogs.confirm({ heading: 'Stop?' },"
                            "                function () { AppController.currentPage = 'export' })"));
    settle(400);

    QObject* const dialog = named("confirmDialog");
    QVERIFY(dialog != nullptr);
    QVERIFY(dialog->property("opened").toBool());

    QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
    settle(400);

    QCOMPARE(evaluate(QStringLiteral("AppController.currentPage")).toString(),
             QStringLiteral("home"));
    QVERIFY(!dialog->property("opened").toBool());
    QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join(u'\n')));
}

QTEST_MAIN(UiSmokeTest)
#include "UiSmokeTest.moc"
