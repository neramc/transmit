// The updater, which is the one part of Transmit that fetches something from
// the internet and then runs it. Every rule that decides whether that happens
// is exercised here, because the cases that matter - a critical fix arriving on
// an unsigned feed, a download one byte short of what was published, a Flatpak
// being told to replace itself - are exactly the ones nobody can arrange by
// hand at the moment they would matter.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "core/update/InstallKind.h"
#include "core/update/UpdateDecision.h"
#include "core/update/UpdateInstaller.h"
#include "core/update/UpdateManifest.h"
#include "core/update/UpdateService.h"
#include "core/update/UpdateSignature.h"
#include "core/update/Version.h"
#include "format/hash/Blake2b.h"

#include "UpdateController.h"

using namespace transmit::core;

namespace {

QByteArray digestOf(const QByteArray& bytes) {
    const auto digest = transmit::format::Blake2b::hash256(
        {reinterpret_cast<const transmit::format::Byte*>(bytes.constData()),
         static_cast<std::size_t>(bytes.size())});
    return QByteArray(reinterpret_cast<const char*>(digest.data()),
                      static_cast<qsizetype>(digest.size()));
}

/// A feed with one release in it, built so a test can bend one field at a time.
QJsonObject artifactJson(const QString& platform, const QString& arch, const QString& kind,
                         const QByteArray& payload, const QString& url) {
    QJsonObject artifact;
    artifact[QStringLiteral("platform")] = platform;
    artifact[QStringLiteral("arch")] = arch;
    artifact[QStringLiteral("kind")] = kind;
    artifact[QStringLiteral("url")] = url;
    artifact[QStringLiteral("size")] = payload.size();
    artifact[QStringLiteral("blake2b")] = QString::fromLatin1(digestOf(payload).toHex());
    return artifact;
}

QJsonObject releaseJson(const QString& version, const QString& severity,
                        const QJsonArray& artifacts) {
    QJsonObject release;
    release[QStringLiteral("version")] = version;
    release[QStringLiteral("severity")] = severity;
    release[QStringLiteral("published")] = QStringLiteral("2026-09-02T00:00:00Z");
    release[QStringLiteral("notes")] = QStringLiteral("what changed");
    release[QStringLiteral("artifacts")] = artifacts;
    return release;
}

QByteArray feedJson(const QJsonArray& releases, const QString& expires = {}) {
    QJsonObject root;
    root[QStringLiteral("schema")] = UpdateManifest::kSchema;
    root[QStringLiteral("generated")] = QStringLiteral("2026-09-02T00:00:00Z");
    if (!expires.isEmpty()) {
        root[QStringLiteral("expires")] = expires;
    }
    root[QStringLiteral("releases")] = releases;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray simpleFeed(const QString& version, const QString& severity, const QByteArray& payload) {
    QJsonArray artifacts;
    artifacts.append(artifactJson(QStringLiteral("linux"), QStringLiteral("x86_64"),
                                  QStringLiteral("appimage"), payload,
                                  QStringLiteral("https://github.com/neramc/transmit/releases/"
                                                 "download/v9.9.9/Transmit.AppImage")));
    QJsonArray releases;
    releases.append(releaseJson(version, severity, artifacts));
    return feedJson(releases);
}

UpdateSituation linuxAppImageOn(const QString& current) {
    UpdateSituation situation;
    situation.current = *Version::parse(current.toStdString());
    situation.installKind = InstallKind::AppImage;
    situation.feedVerified = true;
    situation.updaterEnabled = true;
    situation.preference = UpdatePreference::Notify;
    situation.now = QDateTime::fromString(QStringLiteral("2026-09-02T12:00:00Z"), Qt::ISODate);
    situation.platform = QStringLiteral("linux");
    situation.arch = QStringLiteral("x86_64");
    situation.artifactKind = QStringLiteral("appimage");
    return situation;
}

UpdateManifest parsed(const QByteArray& json) {
    const UpdateManifestReading reading = readUpdateManifest(json);
    return reading.manifest.value_or(UpdateManifest{});
}

/// The first OpenSSL on this machine that can make a one-shot Ed25519
/// signature, or nothing. LibreSSL - which macOS installs as `openssl` - has
/// no `pkeyutl -rawin`, so the answer is not simply "the one on the path".
QString workingOpenssl() {
    QStringList candidates{QStandardPaths::findExecutable(QStringLiteral("openssl"))};
    for (const QString& extra : {QStringLiteral("/opt/homebrew/opt/openssl@3/bin/openssl"),
                                 QStringLiteral("/usr/local/opt/openssl@3/bin/openssl")}) {
        if (QFile::exists(extra)) {
            candidates.append(extra);
        }
    }

    QTemporaryDir area;
    if (!area.isValid()) {
        return {};
    }
    const QString key = area.filePath(QStringLiteral("probe.pem"));
    const QString message = area.filePath(QStringLiteral("probe.bin"));
    {
        QFile file(message);
        if (!file.open(QIODevice::WriteOnly)) {
            return {};
        }
        file.write("probe");
    }

    for (const QString& candidate : candidates) {
        if (candidate.isEmpty()) {
            continue;
        }
        QProcess generate;
        generate.start(candidate, {QStringLiteral("genpkey"), QStringLiteral("-algorithm"),
                                   QStringLiteral("ED25519"), QStringLiteral("-out"), key});
        if (!generate.waitForFinished(15000) || generate.exitCode() != 0) {
            continue;
        }
        QProcess sign;
        sign.start(candidate,
                   {QStringLiteral("pkeyutl"), QStringLiteral("-sign"), QStringLiteral("-inkey"),
                    key, QStringLiteral("-rawin"), QStringLiteral("-in"), message});
        if (sign.waitForFinished(15000) && sign.exitCode() == 0 &&
            sign.readAllStandardOutput().size() == 64) {
            return candidate;
        }
    }
    return {};
}

/// An UpdateService that answers its own requests, so the orchestration can be
/// driven without a network or a signing key.
class StubbedService : public UpdateService {
public:
    QByteArray feed;
    QByteArray signature;
    QByteArray payload;
    bool signatureGood = true;
    bool serveSignature = true;
    bool serveFeed = true;
    /// Bytes actually handed over for the download, which a test can make
    /// differ from what the feed promised.
    std::optional<QByteArray> payloadOverride;
    QString staging;

protected:
    void fetchDocument(const QUrl& url, qint64 cap,
                       std::function<void(bool, QByteArray, QString)> done) override {
        Q_UNUSED(cap);
        if (url.toString().endsWith(QStringLiteral(".sig"))) {
            if (!serveSignature) {
                done(false, {}, QStringLiteral("no signature published"));
                return;
            }
            done(true, signature, {});
            return;
        }
        if (!serveFeed) {
            done(false, {}, QStringLiteral("the server said no"));
            return;
        }
        done(true, feed, {});
    }

    void fetchFile(const QUrl& url, const QString& target, qint64 cap,
                   std::function<void(bool, QString)> done) override {
        Q_UNUSED(url);
        const QByteArray bytes = payloadOverride.value_or(payload);
        if (bytes.size() > cap) {
            done(false, QStringLiteral("the download was larger than the feed said"));
            return;
        }
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly)) {
            done(false, QStringLiteral("could not write"));
            return;
        }
        file.write(bytes);
        file.close();
        done(true, {});
    }

    [[nodiscard]] QString stagingDirectory() const override { return staging; }

    [[nodiscard]] bool feedSignatureIsGood(const QByteArray&, const QByteArray&) const override {
        return signatureGood;
    }
};

}  // namespace

class UpdateTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // The version type
    void readsAThreePartVersion();
    void refusesAnythingThatIsNotOne_data();
    void refusesAnythingThatIsNotOne();
    void ordersVersionsByNumberNotByText();

    // The feed
    void readsAWellFormedFeed();
    void refusesAFeedFromAFutureSchema();
    void refusesAFeedItCannotParse_data();
    void refusesAFeedItCannotParse();
    void refusesASeverityItDoesNotKnowRatherThanGuessing();
    void neverOffersSomethingOlderOrTheSame();
    void noticesAnExpiredFeed();

    // The signature
    void acceptsTheSignaturesFromRfc8032();
    void rejectsASignatureWithOneBitChanged();
    void rejectsASignatureFromAnotherKey();
    void rejectsASignatureFileThatIsNotOne_data();
    void rejectsASignatureFileThatIsNotOne();
    void trustsNothingWhenGivenNoKeys();

    // What to do about it
    void doesNothingWhenAlreadyCurrent();
    void refusesToActOnAnUnsignedFeed();
    void stillSaysACriticalFixIsNeededOnAnUnsignedFeed();
    void willNotReplaceAPackageManagedCopy();
    void willNotActWithoutAnArtifactForThisMachine();
    void offersAnOrdinaryUpdate();
    void installsAnOrdinaryUpdateWhenAskedTo();
    void installsACriticalFixWithoutAsking();
    void treatsACriticalFixAsOrdinaryOnceThePersonIsPastIt();
    void cannotCheckWithoutKnowingItsOwnVersion();
    void cannotCheckWhenTheUpdaterWasLeftOut();

    // The service
    void servesADecisionFromAFeed();
    void downloadsAndVerifiesAnUpdate();
    void refusesADownloadThatDoesNotMatchTheFeed();
    void refusesToDownloadWhatItWasNotAllowedToInstall();

    // The words each answer is given in
    void namesEveryPreference();
    void namesEverySeverity();
    void namesEveryAction();
    void namesEveryShapeOfInstall();
    void saysWhichInstallsMayReplaceThemselves();
    void findsNoArtifactForAMachineTheReleaseDoesNotBuildFor();
    void reportsWhatThisBuildTrusts();
    void readsItsOwnVersion();

    // Remembered between runs
    void remembersWhatItInstalledSoAFailedInstallIsNotRepeated();
    void keepsThePreferenceItWasGiven();

    // The shapes of install it cannot replace on its own
    void handsOverAPortableCopyRatherThanReplacingIt();

    void noticesASandbox();
    void describesTheMachineItIsRunningOn();
    void handsAWindowsUpdateToTheInstaller();

    // The interface's side of it
    void tellsTheInterfaceWhatItFound();
    void doesNotLookAgainStraightAway();
    void saysWhenItCannotInstallWhatItFound();
    void reportsADownloadThatCouldNotBeInstalled();
    void installsACriticalFixTheMomentItFindsOne();

    // The release side and this side agreeing
    void signsAFeedThisBuildAccepts();

    // Putting it in place
    void replacesAFileAndKeepsTheOldOne();
    void putsTheOldOneBack();
    void refusesToInstallOverAPackageManagedCopy();
    void refusesAStagedFileThatIsNotThere();
    void refusesAStagedFileThatChangedAfterItWasChecked();

private:
    QTemporaryDir work_;
};

void UpdateTest::initTestCase() {
    QVERIFY(work_.isValid());

    // Some of this writes a preference and reads it back. Test mode moves
    // QSettings into a directory of its own, so running the suite does not
    // change what the developer's own copy of Transmit does next time they
    // start it.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("TransmitTests"));
    QCoreApplication::setApplicationName(QStringLiteral("UpdateTest"));

    // And emptied, so a second run starts where the first one did. Without
    // this the version remembered by the run before is still there, and the
    // test that says "this has not been installed yet" is answered by
    // yesterday.
    QSettings settings;
    settings.clear();
    settings.sync();
}

// ---------------------------------------------------------------- version ---

void UpdateTest::readsAThreePartVersion() {
    const auto plain = Version::parse("1.2.3");
    QVERIFY(plain.has_value());
    QCOMPARE(plain->major, 1u);
    QCOMPARE(plain->minor, 2u);
    QCOMPARE(plain->patch, 3u);

    // Tags are written with a v, and the feed and the tag should not have to
    // disagree about spelling.
    const auto tagged = Version::parse("v0.10.0");
    QVERIFY(tagged.has_value());
    QCOMPARE(tagged->minor, 10u);
    QCOMPARE(QString::fromStdString(tagged->toString()), QStringLiteral("0.10.0"));
}

void UpdateTest::refusesAnythingThatIsNotOne_data() {
    QTest::addColumn<QString>("text");
    QTest::newRow("empty") << "";
    QTest::newRow("two parts") << "1.2";
    QTest::newRow("four parts") << "1.2.3.4";
    QTest::newRow("leading zero") << "01.2.3";
    QTest::newRow("leading zero, minor") << "1.02.3";
    QTest::newRow("pre-release") << "1.2.3-rc1";
    QTest::newRow("build metadata") << "1.2.3+build";
    QTest::newRow("letters") << "a.b.c";
    QTest::newRow("negative") << "1.2.-3";
    QTest::newRow("space") << "1.2. 3";
    QTest::newRow("trailing dot") << "1.2.3.";
    QTest::newRow("empty part") << "1..3";
    QTest::newRow("too large") << "1.2.4294967296";
    QTest::newRow("just a v") << "v";
}

void UpdateTest::refusesAnythingThatIsNotOne() {
    QFETCH(QString, text);
    QVERIFY2(!Version::parse(text.toStdString()).has_value(), qPrintable(text));
}

void UpdateTest::ordersVersionsByNumberNotByText() {
    // The mistake this guards against is comparing as strings, where "0.9.0"
    // sorts after "0.10.0" and an updater stops offering anything.
    QVERIFY(*Version::parse("0.9.0") < *Version::parse("0.10.0"));
    QVERIFY(*Version::parse("1.0.0") > *Version::parse("0.99.99"));
    QVERIFY(*Version::parse("1.2.3") == *Version::parse("1.2.3"));
    QVERIFY(*Version::parse("1.2.10") > *Version::parse("1.2.9"));
}

// ------------------------------------------------------------------- feed ---

void UpdateTest::readsAWellFormedFeed() {
    const UpdateManifestReading reading =
        readUpdateManifest(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), "body"));
    QVERIFY2(reading.ok(), qPrintable(reading.problem));
    QCOMPARE(reading.manifest->releases.size(), 1);

    const UpdateRelease& release = reading.manifest->releases.first();
    QCOMPARE(QString::fromStdString(release.version.toString()), QStringLiteral("0.2.0"));
    QCOMPARE(release.severity, UpdateSeverity::Normal);
    QCOMPARE(release.artifacts.size(), 1);
    QCOMPARE(release.artifacts.first().blake2b, digestOf("body"));
}

void UpdateTest::refusesAFeedFromAFutureSchema() {
    QByteArray json = simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), "body");
    json.replace("\"schema\":1", "\"schema\":2");

    // Not read as far as it goes. A newer feed may say something this build
    // cannot see, and reading the parts it recognises is acting on half a
    // sentence.
    const UpdateManifestReading reading = readUpdateManifest(json);
    QVERIFY(!reading.ok());
    QVERIFY(reading.problem.contains(QStringLiteral("schema")));
}

void UpdateTest::refusesAFeedItCannotParse_data() {
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("mentions");

    QTest::newRow("empty") << QByteArray() << QStringLiteral("empty");
    QTest::newRow("not json") << QByteArray("{{{") << QStringLiteral("JSON");
    QTest::newRow("not an object") << QByteArray("[]") << QStringLiteral("object");
    QTest::newRow("no schema") << QByteArray(
                                      R"({"generated":"2026-09-02T00:00:00Z","releases":[]})")
                               << QStringLiteral("schema");
    QTest::newRow("no generated time")
        << QByteArray(R"({"schema":1,"releases":[]})") << QStringLiteral("generated");
    QTest::newRow("no releases array")
        << QByteArray(R"({"schema":1,"generated":"2026-09-02T00:00:00Z"})")
        << QStringLiteral("releases");
    QTest::newRow("release is not an object")
        << QByteArray(R"({"schema":1,"generated":"2026-09-02T00:00:00Z","releases":[1]})")
        << QStringLiteral("not an object");
    QTest::newRow("oversized") << (QByteArray(R"({"schema":1,"junk":")") +
                                   QByteArray(300 * 1024, 'x') + QByteArray("\"}"))
                               << QStringLiteral("far more");
}

void UpdateTest::refusesAFeedItCannotParse() {
    QFETCH(QByteArray, json);
    QFETCH(QString, mentions);
    const UpdateManifestReading reading = readUpdateManifest(json);
    QVERIFY(!reading.ok());
    QVERIFY2(reading.problem.contains(mentions, Qt::CaseInsensitive), qPrintable(reading.problem));
}

void UpdateTest::refusesASeverityItDoesNotKnowRatherThanGuessing() {
    // The dangerous default would be Normal: a feed that starts saying
    // "emergency" would be quietly demoted to a suggestion by an older build.
    const UpdateManifestReading reading =
        readUpdateManifest(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("emergency"), "b"));
    QVERIFY(!reading.ok());
    QVERIFY(reading.problem.contains(QStringLiteral("emergency")));
}

void UpdateTest::neverOffersSomethingOlderOrTheSame() {
    QJsonArray releases;
    QJsonArray artifacts;
    artifacts.append(artifactJson(QStringLiteral("linux"), QStringLiteral("x86_64"),
                                  QStringLiteral("appimage"), "b",
                                  QStringLiteral("https://github.com/a/b/c")));
    for (const QString& version :
         {QStringLiteral("0.1.0"), QStringLiteral("0.3.0"), QStringLiteral("0.2.0")}) {
        releases.append(releaseJson(version, QStringLiteral("normal"), artifacts));
    }
    const UpdateManifest manifest = parsed(feedJson(releases));

    QCOMPARE(
        QString::fromStdString(manifest.newestAfter(*Version::parse("0.1.0"))->version.toString()),
        QStringLiteral("0.3.0"));
    QVERIFY(!manifest.newestAfter(*Version::parse("0.3.0")).has_value());
    QVERIFY(!manifest.newestAfter(*Version::parse("9.0.0")).has_value());
}

void UpdateTest::noticesAnExpiredFeed() {
    QJsonArray releases;
    QJsonArray artifacts;
    artifacts.append(artifactJson(QStringLiteral("linux"), QStringLiteral("x86_64"),
                                  QStringLiteral("appimage"), "b",
                                  QStringLiteral("https://github.com/a/b/c")));
    releases.append(releaseJson(QStringLiteral("0.2.0"), QStringLiteral("normal"), artifacts));
    const UpdateManifest manifest =
        parsed(feedJson(releases, QStringLiteral("2026-01-01T00:00:00Z")));

    QVERIFY(manifest.hasExpired(
        QDateTime::fromString(QStringLiteral("2026-09-02T00:00:00Z"), Qt::ISODate)));
    QVERIFY(!manifest.hasExpired(
        QDateTime::fromString(QStringLiteral("2025-12-31T00:00:00Z"), Qt::ISODate)));
}

// -------------------------------------------------------------- signature ---

namespace {
// RFC 8032 section 7.1. Using the standard's own vectors rather than a key
// generated here means the verifier is checked against Ed25519 as specified,
// not against whatever this test would have produced.
struct Rfc8032Case {
    const char* publicKey;
    const char* message;
    const char* signature;
};

constexpr Rfc8032Case kVectors[] = {
    {"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
     "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701c"
     "f9b46bd25bf5f0595bbe24655141438e7a100b"},
    {"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
     "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0"
     "f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
    {"fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
     "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f76098"
     "4dc6594a7c15e9716ed28dc027beceea1ec40a"},
};
}  // namespace

void UpdateTest::acceptsTheSignaturesFromRfc8032() {
    if (!QByteArray(TRANSMIT_UPDATE_TEST_HAS_OPENSSL).startsWith("1")) {
        QSKIP("this build has no OpenSSL, so it cannot check a signature at all");
    }
    for (const Rfc8032Case& vector : kVectors) {
        const QByteArray key = QByteArray::fromHex(vector.publicKey);
        const QByteArray message = QByteArray::fromHex(vector.message);
        const QByteArray signature = QByteArray::fromHex(vector.signature);

        // The empty-message vector cannot go through this path: a signature
        // over nothing is refused before any key is consulted, deliberately.
        if (message.isEmpty()) {
            const SignatureCheck check = verifyDetachedSignature(message, signature, {key});
            QVERIFY(!check.verified);
            continue;
        }

        const SignatureCheck check = verifyDetachedSignature(message, signature, {key});
        QVERIFY2(check.verified, qPrintable(check.problem));
        QCOMPARE(check.keyFingerprint.size(), 16);
    }
}

void UpdateTest::rejectsASignatureWithOneBitChanged() {
    if (!QByteArray(TRANSMIT_UPDATE_TEST_HAS_OPENSSL).startsWith("1")) {
        QSKIP("this build has no OpenSSL");
    }
    const Rfc8032Case& vector = kVectors[1];
    const QByteArray key = QByteArray::fromHex(vector.publicKey);
    const QByteArray message = QByteArray::fromHex(vector.message);

    for (int index : {0, 31, 63}) {
        QByteArray signature = QByteArray::fromHex(vector.signature);
        signature[index] = static_cast<char>(signature[index] ^ 0x01);
        const SignatureCheck check = verifyDetachedSignature(message, signature, {key});
        QVERIFY2(!check.verified, qPrintable(QStringLiteral("byte %1").arg(index)));
    }

    // And the message, which is the direction that matters: a feed edited
    // after signing must not verify.
    QByteArray tampered = message;
    tampered.append('!');
    QVERIFY(
        !verifyDetachedSignature(tampered, QByteArray::fromHex(vector.signature), {key}).verified);
}

void UpdateTest::rejectsASignatureFromAnotherKey() {
    if (!QByteArray(TRANSMIT_UPDATE_TEST_HAS_OPENSSL).startsWith("1")) {
        QSKIP("this build has no OpenSSL");
    }
    const QByteArray message = QByteArray::fromHex(kVectors[1].message);
    const QByteArray signature = QByteArray::fromHex(kVectors[1].signature);
    const QByteArray otherKey = QByteArray::fromHex(kVectors[2].publicKey);

    QVERIFY(!verifyDetachedSignature(message, signature, {otherKey}).verified);

    // With both keys offered it verifies, which is what makes rotating a key
    // possible: old builds keep accepting the old one.
    const QByteArray rightKey = QByteArray::fromHex(kVectors[1].publicKey);
    QVERIFY(verifyDetachedSignature(message, signature, {otherKey, rightKey}).verified);
}

void UpdateTest::rejectsASignatureFileThatIsNotOne_data() {
    QTest::addColumn<QByteArray>("text");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("not base64") << QByteArray("not a signature");
    QTest::newRow("too short") << QByteArray(QByteArray(32, 'a').toBase64());
    QTest::newRow("too long") << QByteArray(QByteArray(65, 'a').toBase64());
    QTest::newRow("enormous") << QByteArray(4096, 'A');
}

void UpdateTest::rejectsASignatureFileThatIsNotOne() {
    QFETCH(QByteArray, text);
    QVERIFY(!readDetachedSignature(text).has_value());
}

void UpdateTest::trustsNothingWhenGivenNoKeys() {
    const QByteArray message = QByteArray::fromHex(kVectors[1].message);
    const QByteArray signature = QByteArray::fromHex(kVectors[1].signature);
    const SignatureCheck check = verifyDetachedSignature(message, signature, {});
    QVERIFY(!check.verified);
    QVERIFY(check.problem.contains(QStringLiteral("no update signing keys")));
}

// --------------------------------------------------------------- decision ---

void UpdateTest::doesNothingWhenAlreadyCurrent() {
    const UpdateManifest manifest =
        parsed(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), "b"));
    const UpdateDecision decision = decideOnUpdate(manifest, linuxAppImageOn("0.2.0"));
    QCOMPARE(decision.action, UpdateAction::NothingToDo);
    QVERIFY(!decision.mandatory);
    QVERIFY(!decision.reason.isEmpty());
}

void UpdateTest::refusesToActOnAnUnsignedFeed() {
    const UpdateManifest manifest =
        parsed(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), "b"));
    UpdateSituation situation = linuxAppImageOn("0.1.0");
    situation.feedVerified = false;

    const UpdateDecision decision = decideOnUpdate(manifest, situation);
    QCOMPARE(decision.action, UpdateAction::TellThemOnly);
    QVERIFY(decision.reason.contains(QStringLiteral("not signed")));
    QVERIFY(!decision.artifact.has_value());
}

void UpdateTest::stillSaysACriticalFixIsNeededOnAnUnsignedFeed() {
    // Nothing may be installed, and the person still has to be told loudly.
    const UpdateManifest manifest =
        parsed(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("critical"), "b"));
    UpdateSituation situation = linuxAppImageOn("0.1.0");
    situation.feedVerified = false;

    const UpdateDecision decision = decideOnUpdate(manifest, situation);
    QCOMPARE(decision.action, UpdateAction::TellThemOnly);
    QVERIFY(decision.mandatory);
}

void UpdateTest::willNotReplaceAPackageManagedCopy() {
    const UpdateManifest manifest =
        parsed(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("critical"), "b"));
    UpdateSituation situation = linuxAppImageOn("0.1.0");
    situation.installKind = InstallKind::PackageManaged;

    const UpdateDecision decision = decideOnUpdate(manifest, situation);
    QCOMPARE(decision.action, UpdateAction::TellThemOnly);
    QVERIFY(decision.mandatory);
    QVERIFY(decision.reason.contains(QStringLiteral("package manager")));
}

void UpdateTest::willNotActWithoutAnArtifactForThisMachine() {
    const UpdateManifest manifest =
        parsed(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("critical"), "b"));
    UpdateSituation situation = linuxAppImageOn("0.1.0");
    situation.arch = QStringLiteral("riscv64");

    const UpdateDecision decision = decideOnUpdate(manifest, situation);
    QCOMPARE(decision.action, UpdateAction::TellThemOnly);
    QVERIFY(decision.reason.contains(QStringLiteral("riscv64")));
}

void UpdateTest::offersAnOrdinaryUpdate() {
    const UpdateManifest manifest =
        parsed(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), "b"));
    const UpdateDecision decision = decideOnUpdate(manifest, linuxAppImageOn("0.1.0"));
    QCOMPARE(decision.action, UpdateAction::Offer);
    QVERIFY(!decision.mandatory);
    QVERIFY(decision.artifact.has_value());
}

void UpdateTest::installsAnOrdinaryUpdateWhenAskedTo() {
    const UpdateManifest manifest =
        parsed(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), "b"));
    UpdateSituation situation = linuxAppImageOn("0.1.0");
    situation.preference = UpdatePreference::Automatic;

    const UpdateDecision decision = decideOnUpdate(manifest, situation);
    QCOMPARE(decision.action, UpdateAction::InstallNow);
    QVERIFY(!decision.mandatory);
}

void UpdateTest::installsACriticalFixWithoutAsking() {
    // The whole point of the severity: the preference says "only when I ask",
    // and a fix for something that loses files or lets somebody in goes on
    // anyway.
    const UpdateManifest manifest =
        parsed(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("critical"), "b"));
    UpdateSituation situation = linuxAppImageOn("0.1.0");
    situation.preference = UpdatePreference::Manual;

    const UpdateDecision decision = decideOnUpdate(manifest, situation);
    QCOMPARE(decision.action, UpdateAction::InstallNow);
    QVERIFY(decision.mandatory);
    QVERIFY(decision.reason.contains(QStringLiteral("without waiting to be asked")));
}

void UpdateTest::treatsACriticalFixAsOrdinaryOnceThePersonIsPastIt() {
    // A critical release can say which versions were actually exposed. Somebody
    // already past that line is offered the update rather than given it.
    QJsonArray artifacts;
    artifacts.append(artifactJson(QStringLiteral("linux"), QStringLiteral("x86_64"),
                                  QStringLiteral("appimage"), "b",
                                  QStringLiteral("https://github.com/a/b/c")));
    QJsonObject release =
        releaseJson(QStringLiteral("0.4.0"), QStringLiteral("critical"), artifacts);
    release[QStringLiteral("unsafeBelow")] = QStringLiteral("0.3.0");
    QJsonArray releases;
    releases.append(release);
    const UpdateManifest manifest = parsed(feedJson(releases));

    UpdateSituation exposed = linuxAppImageOn("0.2.0");
    exposed.preference = UpdatePreference::Manual;
    const UpdateDecision forced = decideOnUpdate(manifest, exposed);
    QCOMPARE(forced.action, UpdateAction::InstallNow);
    QVERIFY(forced.mandatory);

    UpdateSituation safe = linuxAppImageOn("0.3.0");
    safe.preference = UpdatePreference::Manual;
    const UpdateDecision offered = decideOnUpdate(manifest, safe);
    QCOMPARE(offered.action, UpdateAction::Offer);
    QVERIFY(!offered.mandatory);
}

void UpdateTest::cannotCheckWithoutKnowingItsOwnVersion() {
    const UpdateManifest manifest =
        parsed(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("critical"), "b"));
    UpdateSituation situation = linuxAppImageOn("0.1.0");
    situation.current = Version{};

    const UpdateDecision decision = decideOnUpdate(manifest, situation);
    QCOMPARE(decision.action, UpdateAction::CannotCheck);
    QVERIFY(!decision.mandatory);
}

void UpdateTest::cannotCheckWhenTheUpdaterWasLeftOut() {
    const UpdateManifest manifest =
        parsed(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("critical"), "b"));
    UpdateSituation situation = linuxAppImageOn("0.1.0");
    situation.updaterEnabled = false;

    const UpdateDecision decision = decideOnUpdate(manifest, situation);
    QCOMPARE(decision.action, UpdateAction::CannotCheck);
}

// ---------------------------------------------------------------- service ---

void UpdateTest::servesADecisionFromAFeed() {
    StubbedService service;
    service.staging = work_.filePath(QStringLiteral("stage1"));
    service.payload = QByteArray("the new appimage");
    service.feed = simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), service.payload);
    service.signature = QByteArray(64, 'x').toBase64();
    service.overrideSituation(linuxAppImageOn("0.1.0"));

    QSignalSpy checked(&service, &UpdateService::checked);
    service.checkForUpdate();
    QCOMPARE(checked.count(), 1);
    QCOMPARE(service.lastDecision().action, UpdateAction::Offer);

    // And the same feed with the signature rejected installs nothing.
    StubbedService unsigned_;
    unsigned_.staging = work_.filePath(QStringLiteral("stage2"));
    unsigned_.payload = service.payload;
    unsigned_.feed = service.feed;
    unsigned_.signature = service.signature;
    unsigned_.signatureGood = false;
    unsigned_.overrideSituation(linuxAppImageOn("0.1.0"));
    unsigned_.checkForUpdate();
    QCOMPARE(unsigned_.lastDecision().action, UpdateAction::TellThemOnly);
}

void UpdateTest::downloadsAndVerifiesAnUpdate() {
    StubbedService service;
    service.staging = work_.filePath(QStringLiteral("stage3"));
    service.payload = QByteArray("a whole release, for the sake of argument");
    service.feed = simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), service.payload);
    service.signature = QByteArray(64, 'x').toBase64();
    service.overrideSituation(linuxAppImageOn("0.1.0"));

    service.checkForUpdate();
    QCOMPARE(service.lastDecision().action, UpdateAction::Offer);

    QSignalSpy staged(&service, &UpdateService::staged);
    QSignalSpy failed(&service, &UpdateService::failed);
    service.downloadStagedUpdate();

    QCOMPARE(failed.count(), 0);
    QCOMPARE(staged.count(), 1);

    const QString path = staged.first().first().toString();
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), service.payload);
}

void UpdateTest::refusesADownloadThatDoesNotMatchTheFeed() {
    StubbedService service;
    service.staging = work_.filePath(QStringLiteral("stage4"));
    service.payload = QByteArray("what was published");
    service.feed = simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), service.payload);
    service.signature = QByteArray(64, 'x').toBase64();
    // Same length, different bytes: the size check passes and the digest does
    // not, which is the case a size check alone would wave through.
    service.payloadOverride = QByteArray("what was serVed!!!");
    service.overrideSituation(linuxAppImageOn("0.1.0"));

    service.checkForUpdate();
    QSignalSpy staged(&service, &UpdateService::staged);
    QSignalSpy failed(&service, &UpdateService::failed);
    service.downloadStagedUpdate();

    QCOMPARE(staged.count(), 0);
    QCOMPARE(failed.count(), 1);

    // And nothing is left behind for anything else to pick up.
    QDir directory(service.staging);
    QCOMPARE(directory.entryList(QDir::Files).size(), 0);
}

void UpdateTest::refusesToDownloadWhatItWasNotAllowedToInstall() {
    StubbedService service;
    service.staging = work_.filePath(QStringLiteral("stage5"));
    service.payload = QByteArray("body");
    service.feed = simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("critical"), service.payload);
    service.signature = QByteArray(64, 'x').toBase64();
    service.signatureGood = false;
    service.overrideSituation(linuxAppImageOn("0.1.0"));

    service.checkForUpdate();
    QCOMPARE(service.lastDecision().action, UpdateAction::TellThemOnly);

    QSignalSpy staged(&service, &UpdateService::staged);
    QSignalSpy failed(&service, &UpdateService::failed);
    service.downloadStagedUpdate();
    QCOMPARE(staged.count(), 0);
    QCOMPARE(failed.count(), 1);
}

void UpdateTest::reportsADownloadThatCouldNotBeInstalled() {
    // The interface has to say what happened rather than sitting on a
    // spinner: this copy is a build tree, so the download succeeds and the
    // install refuses, and the person is told which.
    auto service = std::make_unique<StubbedService>();
    StubbedService* const stub = service.get();
    stub->staging = work_.filePath(QStringLiteral("controller4"));
    stub->payload = QByteArray("a release nobody here can install");
    stub->feed = simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), stub->payload);
    stub->signature = QByteArray(64, 'x').toBase64();
    stub->overrideSituation(linuxAppImageOn("0.1.0"));

    transmit::app::UpdateController controller(std::move(service), nullptr);
    controller.checkNow();
    QVERIFY(controller.canInstall());

    controller.installNow();
    QVERIFY(!controller.downloading());
    QVERIFY(!controller.installed());
    QVERIFY(!controller.summary().isEmpty());
    QCOMPARE(controller.progressPercent(), 0);

    // And a download that fails outright leaves it in the same shape.
    auto broken = std::make_unique<StubbedService>();
    StubbedService* const brokenStub = broken.get();
    brokenStub->staging = work_.filePath(QStringLiteral("controller5"));
    brokenStub->payload = QByteArray("published");
    brokenStub->feed =
        simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), brokenStub->payload);
    brokenStub->signature = QByteArray(64, 'x').toBase64();
    // The same length, so the size cap passes and the digest is what refuses
    // it - the case a size check on its own would wave through.
    brokenStub->payloadOverride = QByteArray("publisheD");
    brokenStub->overrideSituation(linuxAppImageOn("0.1.0"));

    transmit::app::UpdateController second(std::move(broken), nullptr);
    second.checkNow();
    second.installNow();
    QVERIFY(!second.downloading());
    QVERIFY(!second.installed());
    QVERIFY2(second.summary().contains(QStringLiteral("signed feed")),
             qPrintable(second.summary()));
}

void UpdateTest::installsACriticalFixTheMomentItFindsOne() {
    // The check itself starts the install, without anything being pressed.
    // That is the whole point of the severity, and this is the only place it
    // can be watched happening.
    auto service = std::make_unique<StubbedService>();
    StubbedService* const stub = service.get();
    stub->staging = work_.filePath(QStringLiteral("controller6"));
    stub->payload = QByteArray("the fix");
    stub->feed = simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("critical"), stub->payload);
    stub->signature = QByteArray(64, 'x').toBase64();

    UpdateSituation situation = linuxAppImageOn("0.1.0");
    situation.preference = UpdatePreference::Manual;  // ignored, deliberately
    stub->overrideSituation(situation);

    transmit::app::UpdateController controller(std::move(service), nullptr);
    QSignalSpy restart(&controller, &transmit::app::UpdateController::restartNeeded);
    controller.checkNow();

    QVERIFY(controller.mandatory());
    // It went as far as it could without being asked. Installing fails here
    // because this is a build tree, which is exactly what should stop it.
    QVERIFY(!controller.summary().isEmpty());
    QCOMPARE(restart.count(), 0);
}

// ------------------------------------------------- the two sides agreeing ---

void UpdateTest::signsAFeedThisBuildAccepts() {
    // The one integration that cannot be reasoned about: the script that
    // publishes a feed and the code that reads one have to agree byte for
    // byte, and they are written in different languages by different hands.
    // Everything else in this file tests one side of that.
    if (!QByteArray(TRANSMIT_UPDATE_TEST_HAS_OPENSSL).startsWith("1")) {
        QSKIP("this build has no OpenSSL");
    }
    // macOS ships LibreSSL as `openssl`, and LibreSSL's pkeyutl has no -rawin,
    // which is the only way to make a one-shot Ed25519 signature. Homebrew's
    // OpenSSL 3 is usually beside it. Look for one that can do the job rather
    // than failing on the one that is first on the path.
    const QString openssl = workingOpenssl();
    if (openssl.isEmpty()) {
        QSKIP("no OpenSSL on this machine can make an Ed25519 signature");
    }
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty()) {
        QSKIP("python3 is not on the path");
    }

    QTemporaryDir area;
    QVERIFY(area.isValid());
    const QString keyPath = area.filePath(QStringLiteral("key.pem"));
    const QString releaseDir = area.filePath(QStringLiteral("release"));
    QVERIFY(QDir().mkpath(releaseDir));

    // Says what went wrong. A helper that answers only true or false turns
    // "openssl on this machine cannot do that" into "the test failed", and
    // somebody then spends an afternoon finding out which of five commands it
    // was.
    QString trouble;
    const auto run = [&trouble](const QString& program, const QStringList& arguments,
                                QByteArray* output = nullptr) {
        QProcess process;
        process.start(program, arguments);
        if (!process.waitForFinished(30000)) {
            trouble = QStringLiteral("%1 did not finish").arg(program);
            return false;
        }
        if (output != nullptr) {
            *output = process.readAllStandardOutput();
        }
        if (process.exitCode() != 0) {
            trouble = QStringLiteral("%1 %2 exited %3: %4")
                          .arg(program, arguments.join(u' '))
                          .arg(process.exitCode())
                          .arg(QString::fromUtf8(process.readAllStandardError()).trimmed());
            return false;
        }
        return true;
    };

    QVERIFY(run(QStringLiteral("openssl"),
                {QStringLiteral("genpkey"), QStringLiteral("-algorithm"), QStringLiteral("ED25519"),
                 QStringLiteral("-out"), keyPath}));

    QByteArray publicDer;
    QVERIFY2(run(openssl,
                 {QStringLiteral("pkey"), QStringLiteral("-in"), keyPath, QStringLiteral("-pubout"),
                  QStringLiteral("-outform"), QStringLiteral("DER")},
                 &publicDer),
             qPrintable(trouble));
    QCOMPARE(publicDer.size(), 44);
    const QByteArray publicKey = publicDer.right(32);

    // A file of the shape the release publishes, so the script recognises it.
    const QString artifact =
        QDir(releaseDir).filePath(QStringLiteral("Transmit-0.2.0-linux-x86_64.AppImage"));
    const QByteArray body = QByteArray(
        "\x7f"
        "ELF and then some contents",
        27);
    {
        QFile file(artifact);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(body);
    }

    const QString feedPath = area.filePath(QStringLiteral("updates.json"));

    // The script signs with $OPENSSL, so it uses the one this test satisfied
    // itself can make an Ed25519 signature rather than whatever is first on
    // the path.
    qputenv("OPENSSL", openssl.toLocal8Bit());

    QVERIFY2(
        run(python,
            {QStringLiteral(TRANSMIT_SOURCE_DIR "/scripts/make-update-feed.py"),
             QStringLiteral("--version"), QStringLiteral("0.2.0"), QStringLiteral("--directory"),
             releaseDir, QStringLiteral("--base-url"),
             QStringLiteral("https://github.com/neramc/transmit/releases/download/v0.2.0"),
             QStringLiteral("--out"), feedPath, QStringLiteral("--changelog"),
             QStringLiteral(TRANSMIT_SOURCE_DIR "/CHANGELOG.md"), QStringLiteral("--declaration"),
             QStringLiteral(TRANSMIT_SOURCE_DIR "/packaging/release.json"),
             QStringLiteral("--sign-key"), keyPath}),
        qPrintable(trouble));

    QFile feedFile(feedPath);
    QVERIFY(feedFile.open(QIODevice::ReadOnly));
    const QByteArray feed = feedFile.readAll();
    feedFile.close();

    QFile signatureFile(feedPath + QStringLiteral(".sig"));
    QVERIFY(signatureFile.open(QIODevice::ReadOnly));
    const QByteArray signature = signatureFile.readAll();
    signatureFile.close();

    // The signature the release makes is one this build accepts.
    const SignatureCheck check = verifyDetachedSignature(
        feed, readDetachedSignature(signature).value_or(QByteArray{}), {publicKey});
    QVERIFY2(check.verified, qPrintable(check.problem));

    // The feed the release makes is one this build reads, and the digest it
    // wrote is the digest of the file it was given.
    const UpdateManifestReading reading = readUpdateManifest(feed);
    QVERIFY2(reading.ok(), qPrintable(reading.problem));
    QCOMPARE(reading.manifest->releases.size(), 1);
    const auto found = reading.manifest->releases.first().artifactFor(
        QStringLiteral("linux"), QStringLiteral("x86_64"), QStringLiteral("appimage"));
    QVERIFY(found.has_value());
    QCOMPARE(found->blake2b, digestOf(body));
    QCOMPARE(found->size, static_cast<quint64>(body.size()));

    // And one byte changed anywhere in the feed breaks it, which is what makes
    // the digest above worth anything.
    QByteArray tampered = feed;
    const qsizetype middle = tampered.size() / 2;
    tampered[middle] = static_cast<char>(tampered[middle] ^ 0x01);
    QVERIFY(!verifyDetachedSignature(
                 tampered, readDetachedSignature(signature).value_or(QByteArray{}), {publicKey})
                 .verified);
}

// -------------------------------------------------------------- installer ---

void UpdateTest::replacesAFileAndKeepsTheOldOne() {
    const QString target = work_.filePath(QStringLiteral("Transmit.AppImage"));
    const QString stagedPath = work_.filePath(QStringLiteral("staged.AppImage"));

    QFile old(target);
    QVERIFY(old.open(QIODevice::WriteOnly));
    old.write(
        "\x7f"
        "ELF the old one");
    old.close();

    QFile fresh(stagedPath);
    QVERIFY(fresh.open(QIODevice::WriteOnly));
    fresh.write(
        "\x7f"
        "ELF the new one");
    fresh.close();

    const InstallOutcome outcome =
        UpdateInstaller::apply(stagedPath, target, InstallKind::AppImage);
    QVERIFY2(outcome.applied, qPrintable(outcome.problem));
    QVERIFY(outcome.needsRestart);

    QFile installed(target);
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(), QByteArray("\x7f"
                                             "ELF the new one",
                                             16));
    installed.close();

    QVERIFY(QFile::exists(outcome.previous));
    QFile kept(outcome.previous);
    QVERIFY(kept.open(QIODevice::ReadOnly));
    QCOMPARE(kept.readAll(), QByteArray("\x7f"
                                        "ELF the old one",
                                        16));
}

void UpdateTest::putsTheOldOneBack() {
    const QString target = work_.filePath(QStringLiteral("Undo.AppImage"));
    const QString stagedPath = work_.filePath(QStringLiteral("undo-staged.AppImage"));

    QFile old(target);
    QVERIFY(old.open(QIODevice::WriteOnly));
    old.write(
        "\x7f"
        "ELF original");
    old.close();

    QFile fresh(stagedPath);
    QVERIFY(fresh.open(QIODevice::WriteOnly));
    fresh.write(
        "\x7f"
        "ELF replacement");
    fresh.close();

    const InstallOutcome outcome =
        UpdateInstaller::apply(stagedPath, target, InstallKind::AppImage);
    QVERIFY(outcome.applied);
    QVERIFY(UpdateInstaller::undo(outcome, target));

    QFile restored(target);
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArray("\x7f"
                                            "ELF original",
                                            13));
    QVERIFY(!QFile::exists(outcome.previous));
}

void UpdateTest::refusesToInstallOverAPackageManagedCopy() {
    const QString target = work_.filePath(QStringLiteral("packaged"));
    const QString stagedPath = work_.filePath(QStringLiteral("packaged-staged"));

    QFile fresh(stagedPath);
    QVERIFY(fresh.open(QIODevice::WriteOnly));
    fresh.write(
        "\x7f"
        "ELF");
    fresh.close();

    const InstallOutcome outcome =
        UpdateInstaller::apply(stagedPath, target, InstallKind::PackageManaged);
    QVERIFY(!outcome.applied);
    QVERIFY(outcome.problem.contains(QStringLiteral("package manager")));
    QVERIFY(!QFile::exists(target));
}

void UpdateTest::refusesAStagedFileThatIsNotThere() {
    const QString target = work_.filePath(QStringLiteral("nothing"));
    const InstallOutcome outcome = UpdateInstaller::apply(work_.filePath(QStringLiteral("missing")),
                                                          target, InstallKind::AppImage);
    QVERIFY(!outcome.applied);
    QVERIFY(!QFile::exists(target));
}

void UpdateTest::refusesAStagedFileThatChangedAfterItWasChecked() {
    // Between the download being checked and the moment it is used, the file
    // sits in a cache directory anything running as this user could write to.
    // The digest from the signed feed is checked again there, because that is
    // the last moment it is worth anything.
    const QString target = work_.filePath(QStringLiteral("Swapped.AppImage"));
    const QString stagedPath = work_.filePath(QStringLiteral("swapped-staged.AppImage"));

    QFile old(target);
    QVERIFY(old.open(QIODevice::WriteOnly));
    old.write(
        "\x7f"
        "ELF the one that works");
    old.close();

    const QByteArray downloaded(
        "\x7f"
        "ELF what was published",
        23);
    const QByteArray swapped(
        "\x7f"
        "ELF what turned up now",
        23);
    QCOMPARE(downloaded.size(), swapped.size());

    QFile fresh(stagedPath);
    QVERIFY(fresh.open(QIODevice::WriteOnly));
    fresh.write(swapped);
    fresh.close();

    const InstallOutcome outcome =
        UpdateInstaller::apply(stagedPath, target, InstallKind::AppImage, digestOf(downloaded));
    QVERIFY(!outcome.applied);
    QVERIFY(outcome.problem.contains(QStringLiteral("not the file that was downloaded")));

    // The running program is untouched, and the file that was not what it
    // claimed is gone rather than left for something else to pick up.
    QFile installed(target);
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(), QByteArray("\x7f"
                                             "ELF the one that works",
                                             23));
    QVERIFY(!QFile::exists(stagedPath));

    // And the same file with the digest it actually has goes on.
    QFile again(stagedPath);
    QVERIFY(again.open(QIODevice::WriteOnly));
    again.write(swapped);
    again.close();
    const InstallOutcome allowed =
        UpdateInstaller::apply(stagedPath, target, InstallKind::AppImage, digestOf(swapped));
    QVERIFY2(allowed.applied, qPrintable(allowed.problem));
}

// --------------------------------------------------- what things are called ---

void UpdateTest::namesEveryPreference() {
    // Every value round trips through the name it is stored under, because the
    // preference is written to disk as text and read back by a later version.
    for (const UpdatePreference preference :
         {UpdatePreference::Manual, UpdatePreference::Notify, UpdatePreference::Automatic}) {
        const QString stored = toString(preference);
        QVERIFY(!stored.isEmpty());
        // QVERIFY rather than QCOMPARE: QtTest prints a mismatched value with
        // its own toString, finds ours by argument-dependent lookup, and does
        // not compile because ours returns a QString.
        QVERIFY(preferenceFromString(stored).value_or(UpdatePreference::Automatic) == preference);
        QVERIFY(!describe(preference).isEmpty());
    }
    QVERIFY(!preferenceFromString(QStringLiteral("whenever")).has_value());
    QVERIFY(!preferenceFromString(QString()).has_value());
}

void UpdateTest::namesEverySeverity() {
    for (const UpdateSeverity severity :
         {UpdateSeverity::Normal, UpdateSeverity::Important, UpdateSeverity::Critical}) {
        const QString name = describe(severity);
        QVERIFY(!name.isEmpty());
        QCOMPARE(severityFromString(name).value_or(UpdateSeverity::Normal), severity);
    }
    QVERIFY(!severityFromString(QStringLiteral("emergency")).has_value());
}

void UpdateTest::namesEveryAction() {
    for (const UpdateAction action :
         {UpdateAction::NothingToDo, UpdateAction::CannotCheck, UpdateAction::TellThemOnly,
          UpdateAction::Offer, UpdateAction::InstallNow}) {
        QVERIFY(!describe(action).isEmpty());
    }
}

void UpdateTest::namesEveryShapeOfInstall() {
    // Every one of these ends up in a sentence somebody reads when Transmit
    // declines to update itself, so none of them may be empty.
    for (const InstallKind kind :
         {InstallKind::Unknown, InstallKind::AppImage, InstallKind::WindowsInstaller,
          InstallKind::WindowsPortable, InstallKind::MacBundle, InstallKind::PackageManaged,
          InstallKind::Development}) {
        QVERIFY2(!describe(kind).isEmpty(), qPrintable(QString::number(static_cast<int>(kind))));
    }
}

void UpdateTest::saysWhichInstallsMayReplaceThemselves() {
    QVERIFY(canReplaceItself(InstallKind::AppImage));
    QVERIFY(canReplaceItself(InstallKind::WindowsInstaller));
    QVERIFY(canReplaceItself(InstallKind::WindowsPortable));
    QVERIFY(canReplaceItself(InstallKind::MacBundle));

    // The three that must never be replaced from inside: one a package
    // manager owns, one nobody could identify, and a build tree.
    QVERIFY(!canReplaceItself(InstallKind::PackageManaged));
    QVERIFY(!canReplaceItself(InstallKind::Unknown));
    QVERIFY(!canReplaceItself(InstallKind::Development));

    // And nothing that cannot replace itself names something to replace.
    for (const InstallKind kind :
         {InstallKind::PackageManaged, InstallKind::Unknown, InstallKind::Development}) {
        QVERIFY(replaceableTarget(kind).isEmpty());
    }

    // This test runs from a build tree, and Transmit should say so rather
    // than mistaking it for an installation.
    QCOMPARE(detectInstallKind(), InstallKind::Development);
}

void UpdateTest::findsNoArtifactForAMachineTheReleaseDoesNotBuildFor() {
    const UpdateManifest manifest =
        parsed(simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), "b"));
    const UpdateRelease& release = manifest.releases.first();

    QVERIFY(release
                .artifactFor(QStringLiteral("linux"), QStringLiteral("x86_64"),
                             QStringLiteral("appimage"))
                .has_value());
    QVERIFY(!release
                 .artifactFor(QStringLiteral("linux"), QStringLiteral("arm64"),
                              QStringLiteral("appimage"))
                 .has_value());
    QVERIFY(!release
                 .artifactFor(QStringLiteral("windows"), QStringLiteral("x86_64"),
                              QStringLiteral("appimage"))
                 .has_value());
    QVERIFY(
        !release
             .artifactFor(QStringLiteral("linux"), QStringLiteral("x86_64"), QStringLiteral("deb"))
             .has_value());
}

void UpdateTest::reportsWhatThisBuildTrusts() {
    // This build is given no keys, so it can check nothing - and says so
    // rather than quietly accepting.
    QCOMPARE(canVerifyUpdates(), !trustedUpdateKeys().isEmpty());

    const SignatureCheck check =
        verifyUpdateSignature(QByteArray("a feed"), QByteArray(64, 'x').toBase64());
    QVERIFY(!check.verified);
    QVERIFY(!check.problem.isEmpty());

    // A signature file that is not one is refused before any key is consulted.
    const SignatureCheck malformed = verifyUpdateSignature(QByteArray("a feed"), QByteArray("no"));
    QVERIFY(!malformed.verified);
    QVERIFY(malformed.problem.contains(QStringLiteral("base64")));
}

void UpdateTest::readsItsOwnVersion() {
    const auto running = runningVersion();
    QVERIFY(running.has_value());
    QCOMPARE(QString::fromStdString(running->toString()), QString::fromLatin1(TRANSMIT_VERSION));
    QVERIFY(!running->isZero());
    QVERIFY(Version{}.isZero());
}

// ------------------------------------------------------------- remembered ---

void UpdateTest::remembersWhatItInstalledSoAFailedInstallIsNotRepeated() {
    QVERIFY(!UpdateService::wouldRepeatAFailedInstall(QStringLiteral("9.9.9")));
    QVERIFY(!UpdateService::wouldRepeatAFailedInstall(QString()));

    UpdateService::rememberInstalled(QStringLiteral("9.9.9"));
    QCOMPARE(UpdateService::lastInstalledVersion(), QStringLiteral("9.9.9"));
    QVERIFY(UpdateService::lastInstalledAt().isValid());

    // The same version again, straight afterwards, is an install that did not
    // take rather than one worth trying.
    QVERIFY(UpdateService::wouldRepeatAFailedInstall(QStringLiteral("9.9.9")));
    QVERIFY(!UpdateService::wouldRepeatAFailedInstall(QStringLiteral("9.9.10")));
}

void UpdateTest::keepsThePreferenceItWasGiven() {
    const UpdatePreference original = UpdateService::preference();
    UpdateService::setPreference(UpdatePreference::Automatic);
    QVERIFY(UpdateService::preference() == UpdatePreference::Automatic);
    UpdateService::setPreference(UpdatePreference::Manual);
    QVERIFY(UpdateService::preference() == UpdatePreference::Manual);
    UpdateService::setPreference(original);

    // The two addresses are compiled in and have to be usable as they stand.
    QVERIFY(UpdateService::feedUrl().isValid());
    QCOMPARE(UpdateService::feedUrl().scheme(), QStringLiteral("https"));
    QVERIFY(UpdateService::releasesPage().isValid());
    QCOMPARE(UpdateService::releasesPage().scheme(), QStringLiteral("https"));

    // Running from a build tree, nothing may be installed whatever else is true.
    QVERIFY(!UpdateService::canInstallUpdates());
}

// ------------------------------------------ what it cannot replace itself ---

void UpdateTest::handsOverAPortableCopyRatherThanReplacingIt() {
    // A portable copy and a bundle are directories somebody unpacked or
    // dragged. Replacing either from inside the program that lives in it is
    // how half-copied installs happen, so the download is checked and left
    // where they can find it.
    const QString stagedPath = work_.filePath(QStringLiteral("handed-over"));
    QFile fresh(stagedPath);
    QVERIFY(fresh.open(QIODevice::WriteOnly));
    fresh.write(
        "\x7f"
        "ELF a whole release");
    fresh.close();

    for (const InstallKind kind : {InstallKind::WindowsPortable, InstallKind::MacBundle}) {
        const InstallOutcome outcome =
            UpdateInstaller::apply(stagedPath, work_.filePath(QStringLiteral("somewhere")), kind);
        QVERIFY(!outcome.applied);
        QCOMPARE(outcome.handedOver, stagedPath);
        QVERIFY(!outcome.problem.isEmpty());
        QVERIFY(QFile::exists(stagedPath));
    }

    // And undo has nothing to put back when nothing was moved.
    InstallOutcome nothing;
    QVERIFY(UpdateInstaller::undo(nothing, work_.filePath(QStringLiteral("somewhere"))));
}

void UpdateTest::noticesASandbox() {
    // A sandbox is recognised before anything else, because every guess below
    // it would be wrong: a Flatpak has no network, no writable program
    // directory, and something else that updates it.
    qputenv("FLATPAK_ID", "io.github.neramc.Transmit");
    QCOMPARE(detectInstallKind(), InstallKind::PackageManaged);
    QVERIFY(replaceableTarget(detectInstallKind()).isEmpty());
    qunsetenv("FLATPAK_ID");

    qputenv("SNAP", "/snap/transmit/current");
    QCOMPARE(detectInstallKind(), InstallKind::PackageManaged);
    qunsetenv("SNAP");

    // And with neither, this is a build tree again.
    QCOMPARE(detectInstallKind(), InstallKind::Development);
}

void UpdateTest::describesTheMachineItIsRunningOn() {
    const UpdateSituation situation = situationForThisBuild();

    QVERIFY(!situation.platform.isEmpty());
    QVERIFY(!situation.arch.isEmpty());
    QVERIFY(situation.now.isValid());
    QVERIFY(!situation.current.isZero());
    QVERIFY(!situation.feedVerified);  // nothing has been checked yet

    // Running from a build tree there is no artifact kind to look for, which
    // is what stops the decision offering something to install.
    QCOMPARE(situation.installKind, InstallKind::Development);
    QVERIFY(situation.artifactKind.isEmpty());

#ifdef TRANSMIT_UPDATER_ENABLED
    QVERIFY(situation.updaterEnabled);
#else
    QVERIFY(!situation.updaterEnabled);
#endif
}

void UpdateTest::handsAWindowsUpdateToTheInstaller() {
    // On Windows the update is an installer this program starts and then gets
    // out of the way of, rather than a file it overwrites itself with. The
    // branch is compiled everywhere, so it can be taken here: what is checked
    // is that a staged file that is not a program is refused before anything
    // is started.
    const QString stagedPath = work_.filePath(QStringLiteral("setup-that-is-not"));
    QFile notAProgram(stagedPath);
    QVERIFY(notAProgram.open(QIODevice::WriteOnly));
    notAProgram.write("this is not an executable");
    notAProgram.close();

    const InstallOutcome refused = UpdateInstaller::apply(
        stagedPath, work_.filePath(QStringLiteral("installed")), InstallKind::WindowsInstaller);
    QVERIFY(!refused.applied);
    QVERIFY(refused.problem.contains(QStringLiteral("not a program")));

    // And the same guard on the AppImage path.
    const InstallOutcome alsoRefused = UpdateInstaller::apply(
        stagedPath, work_.filePath(QStringLiteral("installed")), InstallKind::AppImage);
    QVERIFY(!alsoRefused.applied);

    // A staged file that is the running program is refused rather than moved
    // over itself.
    const InstallOutcome itself =
        UpdateInstaller::apply(stagedPath, stagedPath, InstallKind::AppImage);
    QVERIFY(!itself.applied);
    QVERIFY(itself.problem.contains(QStringLiteral("running program")));

    // And nothing to replace is not something to guess at.
    const InstallOutcome nowhere =
        UpdateInstaller::apply(stagedPath, QString(), InstallKind::AppImage);
    QVERIFY(!nowhere.applied);
}

// ---------------------------------------------------------- the interface ---

void UpdateTest::tellsTheInterfaceWhatItFound() {
    auto service = std::make_unique<StubbedService>();
    StubbedService* const stub = service.get();
    stub->staging = work_.filePath(QStringLiteral("controller1"));
    stub->payload = QByteArray("a release for the interface");
    stub->feed = simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), stub->payload);
    stub->signature = QByteArray(64, 'x').toBase64();
    stub->overrideSituation(linuxAppImageOn("0.1.0"));

    transmit::app::UpdateController controller(std::move(service), nullptr);
    QSignalSpy changed(&controller, &transmit::app::UpdateController::stateChanged);

    QVERIFY(!controller.updateAvailable());
    controller.checkNow();

    QVERIFY(changed.count() >= 2);  // looking, then the answer
    QVERIFY(!controller.checking());
    QVERIFY(controller.updateAvailable());
    QVERIFY(controller.canInstall());
    QVERIFY(!controller.mandatory());
    QCOMPARE(controller.availableVersion(), QStringLiteral("0.2.0"));
    QCOMPARE(controller.severity(), QStringLiteral("normal"));
    QVERIFY(!controller.summary().isEmpty());
    QVERIFY(!controller.notes().isEmpty());
    QVERIFY(!controller.releasesPage().isEmpty());
    QVERIFY(!controller.installKind().isEmpty());
    QVERIFY(!controller.lastChecked().isEmpty());
    QVERIFY(!controller.installed());

    // And the preference goes through it to the service and back.
    controller.setPreference(QStringLiteral("automatic"));
    QCOMPARE(controller.preference(), QStringLiteral("automatic"));
    controller.setPreference(QStringLiteral("nonsense"));
    QCOMPARE(controller.preference(), QStringLiteral("automatic"));
    controller.setPreference(QStringLiteral("notify"));
}

void UpdateTest::doesNotLookAgainStraightAway() {
    // A background check is a request to somebody else's server on every
    // start otherwise, and the answer does not change that fast.
    auto service = std::make_unique<StubbedService>();
    StubbedService* const stub = service.get();
    stub->staging = work_.filePath(QStringLiteral("controller2"));
    stub->payload = QByteArray("body");
    stub->feed = simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("normal"), stub->payload);
    stub->signature = QByteArray(64, 'x').toBase64();
    stub->overrideSituation(linuxAppImageOn("0.1.0"));

    transmit::app::UpdateController controller(std::move(service), nullptr);
    controller.checkNow();
    QVERIFY(controller.updateAvailable());

    // Just checked, so a quiet check finds nothing to do and leaves the
    // answer alone.
    const QString before = controller.summary();
    controller.checkQuietly();
    QCOMPARE(controller.summary(), before);

    // And the environment says not to at all, which is what keeps the tests
    // and the startup measurement off the network.
    qputenv("TRANSMIT_NO_UPDATE_CHECK", "1");
    controller.checkQuietly();
    QCOMPARE(controller.summary(), before);
    qunsetenv("TRANSMIT_NO_UPDATE_CHECK");
}

void UpdateTest::saysWhenItCannotInstallWhatItFound() {
    auto service = std::make_unique<StubbedService>();
    StubbedService* const stub = service.get();
    stub->staging = work_.filePath(QStringLiteral("controller3"));
    stub->payload = QByteArray("body");
    stub->feed = simpleFeed(QStringLiteral("0.2.0"), QStringLiteral("critical"), stub->payload);
    stub->signature = QByteArray(64, 'x').toBase64();
    stub->signatureGood = false;

    UpdateSituation packaged = linuxAppImageOn("0.1.0");
    packaged.installKind = InstallKind::PackageManaged;
    stub->overrideSituation(packaged);

    transmit::app::UpdateController controller(std::move(service), nullptr);
    controller.checkNow();

    // Loud about needing it, and unable to do anything about it.
    QVERIFY(controller.updateAvailable());
    QVERIFY(controller.mandatory());
    QVERIFY(!controller.canInstall());
    QVERIFY(!controller.installingUnasked());

    // Asking anyway changes nothing.
    controller.installNow();
    QVERIFY(!controller.downloading());
    QVERIFY(!controller.installed());
}

QTEST_MAIN(UpdateTest)
#include "UpdateTest.moc"
