// A selection written down and read back.
//
// The point of the document is that a capture can be repeated exactly - by the
// same person next month, by somebody else on another machine, or by a test.
// That only holds if everything survives the round trip, so this checks every
// field rather than the handful that are easy to get wrong.

#include <QTest>

#include "core/continuity/SelectionCodec.h"

using namespace transmit;

class SelectionCodecTest : public QObject {
    Q_OBJECT

private slots:
    void everyFieldSurvivesTheRoundTrip();
    void aShortDocumentTakesTheDefaults();
    void theSameSelectionAlwaysProducesTheSameBytes();
    void aDocumentFromANewerVersionIsRefused();
    void aRootThatClimbsOutOfItsFolderIsRefused();
    void nonsenseIsReportedRatherThanGuessedAt();
};

void SelectionCodecTest::everyFieldSurvivesTheRoundTrip() {
    core::CaptureDocument original;
    original.label = QStringLiteral("the laptop, before the reinstall");

    original.selection.domains = {static_cast<int>(format::DomainId::UserData),
                                  static_cast<int>(format::DomainId::AppState)};

    original.selection.scope.maximumFileSize = 2ULL * 1024 * 1024 * 1024;
    original.selection.scope.minimumFileSize = 16;
    original.selection.scope.includeExtensions = {QStringLiteral("txt"), QStringLiteral("md")};
    original.selection.scope.excludeExtensions = {QStringLiteral("iso")};
    original.selection.scope.modifiedSince =
        QDateTime::fromString(QStringLiteral("2024-01-01T00:00:00"), Qt::ISODate);
    original.selection.scope.modifiedBefore =
        QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00"), Qt::ISODate);
    original.selection.scope.excludePatterns = {QStringLiteral("**/node_modules/**")};
    original.selection.scope.followSymlinks = true;
    original.selection.scope.includeHidden = false;

    core::CaptureRoot documents;
    documents.token = format::PathTokenId::Documents;
    documents.relative = QStringLiteral("work/reports");
    documents.domain = format::DomainId::UserData;
    documents.excludePatterns = {QStringLiteral("**/*.bak")};
    documents.scope.maximumFileSize = 1024;
    original.selection.roots.push_back(documents);

    core::CaptureRoot firefox;
    firefox.token = format::PathTokenId::AppConfig;
    firefox.relative = QStringLiteral("Mozilla/Firefox");
    firefox.domain = format::DomainId::AppState;
    firefox.appId = QStringLiteral("org.mozilla.firefox");
    firefox.stateRootId = QStringLiteral("profile");
    firefox.recursive = false;
    firefox.isFallback = true;
    original.selection.roots.push_back(firefox);

    original.selection.appMode = core::AppSelectionMode::Explicit;
    core::AppSelection app;
    app.appId = QStringLiteral("org.mozilla.firefox");
    app.captureState = true;
    app.recordForReinstall = false;
    app.stateRootIds = {QStringLiteral("profile")};
    app.scope.excludeExtensions = {QStringLiteral("sqlite-wal")};
    original.selection.apps.push_back(app);

    original.packaging.preset = format::CompressionPreset::Fast;
    original.packaging.partSize = 3584ULL * 1024 * 1024;
    original.packaging.solidBlockSize = 16 * 1024 * 1024;
    original.packaging.workerCount = 3;
    original.packaging.syncIntervalBytes = 32 * 1024 * 1024;
    original.packaging.verifyAfterWriting = false;

    const QByteArray encoded = core::SelectionCodec::encode(original);

    core::CaptureDocument restored;
    QString error;
    QVERIFY2(core::SelectionCodec::decode(encoded, restored, &error), qPrintable(error));

    QCOMPARE(restored.label, original.label);
    QCOMPARE(restored.selection.domains, original.selection.domains);
    QCOMPARE(restored.selection.appMode, original.selection.appMode);

    const core::ScopeRule& scope = restored.selection.scope;
    QCOMPARE(scope.maximumFileSize, original.selection.scope.maximumFileSize);
    QCOMPARE(scope.minimumFileSize, original.selection.scope.minimumFileSize);
    QCOMPARE(scope.includeExtensions, original.selection.scope.includeExtensions);
    QCOMPARE(scope.excludeExtensions, original.selection.scope.excludeExtensions);
    QCOMPARE(scope.modifiedSince, original.selection.scope.modifiedSince);
    QCOMPARE(scope.modifiedBefore, original.selection.scope.modifiedBefore);
    QCOMPARE(scope.excludePatterns, original.selection.scope.excludePatterns);
    QCOMPARE(scope.followSymlinks, original.selection.scope.followSymlinks);
    QCOMPARE(scope.includeHidden, original.selection.scope.includeHidden);

    QCOMPARE(restored.selection.roots.size(), 2);
    QCOMPARE(restored.selection.roots[0].token, documents.token);
    QCOMPARE(restored.selection.roots[0].relative, documents.relative);
    QCOMPARE(restored.selection.roots[0].excludePatterns, documents.excludePatterns);
    QCOMPARE(restored.selection.roots[0].scope.maximumFileSize, documents.scope.maximumFileSize);
    QCOMPARE(restored.selection.roots[1].appId, firefox.appId);
    QCOMPARE(restored.selection.roots[1].stateRootId, firefox.stateRootId);
    QCOMPARE(restored.selection.roots[1].recursive, firefox.recursive);
    QCOMPARE(restored.selection.roots[1].isFallback, firefox.isFallback);

    QCOMPARE(restored.selection.apps.size(), 1);
    QCOMPARE(restored.selection.apps[0].appId, app.appId);
    QCOMPARE(restored.selection.apps[0].captureState, app.captureState);
    QCOMPARE(restored.selection.apps[0].recordForReinstall, app.recordForReinstall);
    QCOMPARE(restored.selection.apps[0].stateRootIds, app.stateRootIds);
    QCOMPARE(restored.selection.apps[0].scope.excludeExtensions, app.scope.excludeExtensions);

    QCOMPARE(restored.packaging.preset, original.packaging.preset);
    QCOMPARE(restored.packaging.partSize, original.packaging.partSize);
    QCOMPARE(restored.packaging.solidBlockSize, original.packaging.solidBlockSize);
    QCOMPARE(restored.packaging.workerCount, original.packaging.workerCount);
    QCOMPARE(restored.packaging.syncIntervalBytes, original.packaging.syncIntervalBytes);
    QCOMPARE(restored.packaging.verifyAfterWriting, original.packaging.verifyAfterWriting);
}

void SelectionCodecTest::aShortDocumentTakesTheDefaults() {
    // A file naming only what somebody cares about is valid. Requiring every
    // field would make the format unusable by hand, which is most of the point
    // of it being text.
    const QByteArray minimal = R"({ "domains": ["userdata"] })";

    core::CaptureDocument document;
    QString error;
    QVERIFY2(core::SelectionCodec::decode(minimal, document, &error), qPrintable(error));

    QCOMPARE(document.selection.domains.size(), 1);
    QVERIFY(document.selection.includes(format::DomainId::UserData));
    QVERIFY(document.selection.scope.isUnrestricted());
    QCOMPARE(document.selection.appMode, core::AppSelectionMode::All);
    QVERIFY(document.packaging.verifyAfterWriting);
    QCOMPARE(document.packaging.solidBlockSize, format::kDefaultSolidBlockSize);
}

void SelectionCodecTest::theSameSelectionAlwaysProducesTheSameBytes() {
    // Sets have no order of their own, so an encoder that walked one directly
    // would produce a different file each run - useless in version control and
    // useless for telling two captures apart.
    core::CaptureDocument document;
    document.selection.domains = {static_cast<int>(format::DomainId::UserData)};
    document.selection.scope.includeExtensions = {QStringLiteral("txt"), QStringLiteral("md"),
                                                  QStringLiteral("odt"), QStringLiteral("pdf"),
                                                  QStringLiteral("rtf"), QStringLiteral("doc")};

    const QByteArray first = core::SelectionCodec::encode(document);
    for (int attempt = 0; attempt < 20; ++attempt) {
        QCOMPARE(core::SelectionCodec::encode(document), first);
    }
}

void SelectionCodecTest::aDocumentFromANewerVersionIsRefused() {
    // Read as far as it goes, a newer document would silently drop a choice
    // this build does not know about - and capture something other than what
    // it says on the tin.
    const QByteArray future = R"({ "selectionVersion": 99, "domains": ["userdata"] })";

    core::CaptureDocument document;
    QString error;
    QVERIFY(!core::SelectionCodec::decode(future, document, &error));
    QVERIFY2(error.contains(QStringLiteral("newer")), qPrintable(error));
}

void SelectionCodecTest::aRootThatClimbsOutOfItsFolderIsRefused() {
    // A selection is a file somebody may have been sent.
    const QByteArray escaping = R"({
        "domains": ["userdata"],
        "roots": [{ "token": "{DOCUMENTS}", "relative": "../../etc", "domain": "userdata" }]
    })";

    core::CaptureDocument document;
    QString error;
    QVERIFY(!core::SelectionCodec::decode(escaping, document, &error));
    QVERIFY2(error.contains(QStringLiteral("..")), qPrintable(error));
}

void SelectionCodecTest::nonsenseIsReportedRatherThanGuessedAt() {
    core::CaptureDocument document;
    QString error;

    QVERIFY(!core::SelectionCodec::decode(QByteArray("not json at all"), document, &error));
    QVERIFY(!error.isEmpty());

    QVERIFY(!core::SelectionCodec::decode(QByteArray(R"({"domains": ["nonsense"]})"), document,
                                          &error));
    QVERIFY2(error.contains(QStringLiteral("nonsense")), qPrintable(error));

    QVERIFY(!core::SelectionCodec::decode(QByteArray(R"({"domains": []})"), document, &error));
    QVERIFY(!error.isEmpty());

    QVERIFY(!core::SelectionCodec::decode(QByteArray(R"({"domains": ["userdata"],
                       "roots": [{"token": "{NOWHERE}", "domain": "userdata"}]})"),
                                          document, &error));
    QVERIFY2(error.contains(QStringLiteral("NOWHERE")), qPrintable(error));

    QVERIFY(!core::SelectionCodec::decode(
        QByteArray(R"({"domains": ["userdata"], "packaging": {"preset": "turbo"}})"), document,
        &error));
    QVERIFY2(error.contains(QStringLiteral("turbo")), qPrintable(error));
}

QTEST_MAIN(SelectionCodecTest)
#include "SelectionCodecTest.moc"
