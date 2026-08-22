#include <QTest>

#include "platform/PlatformService.h"
#include "platform/SecretStore.h"

using namespace transmit;

/// The credential store, against the real one on this machine.
///
/// Everything here needs a running secret service, which a build machine
/// usually does not have, so the whole suite skips rather than fails when
/// there is none. Run it for real with:
///
///     dbus-run-session -- scripts/with-keyring.sh \
///         ./build/tests/integration/transmit_SecretStore_test
class SecretStoreTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void reportsWhatItCanReach();
    void anEntryComesBackWithItsSecret();
    void theAttributesAnApplicationWroteSurvive();
    void aBrowserLoginIsRecognisedAsOne();

private:
    /// A label unlikely to collide with anything the machine already holds.
    [[nodiscard]] static QString testLabel(const QString& suffix);

    /// The record this test stored, found by its service attribute.
    [[nodiscard]] platform::SecretRecord findByService(const QString& service) const;

    std::unique_ptr<platform::PlatformService> platform_;
    std::unique_ptr<platform::SecretStore> store_;
};

QString SecretStoreTest::testLabel(const QString& suffix) {
    return QStringLiteral("transmit-test-%1").arg(suffix);
}

void SecretStoreTest::initTestCase() {
    platform_ = platform::PlatformService::create();
    store_ = platform_->secretStore();
    QVERIFY(store_ != nullptr);

    if (!store_->isAvailable()) {
        QSKIP("no credential store on this machine");
    }

    // Writing then reading back is the only honest probe: a build without
    // libsecret reports the keyring as available because it can still write.
    platform::SecretRecord probe;
    probe.kind = platform::SecretKind::ApplicationPassword;
    probe.service = testLabel(QStringLiteral("probe"));
    probe.account = QStringLiteral("nobody");
    probe.label = testLabel(QStringLiteral("probe"));
    probe.secret = QStringLiteral("probe-value");
    probe.attributes.insert(QStringLiteral("service"), probe.service);
    probe.attributes.insert(QStringLiteral("account"), probe.account);

    if (store_->store(probe).outcome != platform::ApplyOutcome::Applied) {
        QSKIP("this machine's credential store would not accept an entry");
    }
    if (findByService(testLabel(QStringLiteral("probe"))).secret.isEmpty()) {
        QSKIP("this build cannot read the credential store back");
    }
}

platform::SecretRecord SecretStoreTest::findByService(const QString& service) const {
    for (const platform::SecretRecord& record :
         store_->read(/*includeWifi=*/false, /*includeApplications=*/true)) {
        if (record.service == service || record.attributes.value(QStringLiteral("service")) == service ||
            record.attributes.value(QStringLiteral("signon_realm")) == service) {
            return record;
        }
    }
    return {};
}

void SecretStoreTest::reportsWhatItCanReach() {
    QVERIFY(store_->isAvailable());
    QVERIFY2(!store_->describe().isEmpty(), "the report has to be able to name the store");
}

void SecretStoreTest::anEntryComesBackWithItsSecret() {
    const QString service = testLabel(QStringLiteral("mail"));

    platform::SecretRecord written;
    written.kind = platform::SecretKind::ApplicationPassword;
    written.service = service;
    written.account = QStringLiteral("alice");
    written.label = QStringLiteral("Test mail account");
    written.secret = QStringLiteral("a passphrase with spaces and ünïcode");
    written.attributes.insert(QStringLiteral("service"), service);
    written.attributes.insert(QStringLiteral("account"), QStringLiteral("alice"));

    QCOMPARE(store_->store(written).outcome, platform::ApplyOutcome::Applied);

    const platform::SecretRecord found = findByService(service);
    QVERIFY2(!found.secret.isEmpty(), "the entry should have come back");
    QCOMPARE(found.secret, written.secret);
    QCOMPARE(found.account, QStringLiteral("alice"));
}

void SecretStoreTest::theAttributesAnApplicationWroteSurvive() {
    // An application finds its own entry by the attributes it chose. Putting
    // one back with only a service and an account would leave the entry
    // present but invisible to the program it belongs to.
    const QString service = testLabel(QStringLiteral("fileserver"));

    platform::SecretRecord written;
    written.kind = platform::SecretKind::NetworkCredential;
    written.service = service;
    written.account = QStringLiteral("carol");
    written.label = QStringLiteral("Test fileserver");
    written.secret = QStringLiteral("topsecret");
    written.attributes.insert(QStringLiteral("service"), service);
    written.attributes.insert(QStringLiteral("user"), QStringLiteral("carol"));
    written.attributes.insert(QStringLiteral("protocol"), QStringLiteral("smb"));
    written.attributes.insert(QStringLiteral("port"), QStringLiteral("445"));

    QCOMPARE(store_->store(written).outcome, platform::ApplyOutcome::Applied);

    const platform::SecretRecord found = findByService(service);
    QVERIFY(!found.secret.isEmpty());
    QCOMPARE(found.attributes.value(QStringLiteral("protocol")), QStringLiteral("smb"));
    QCOMPARE(found.attributes.value(QStringLiteral("port")), QStringLiteral("445"));
    QCOMPARE(found.attributes.value(QStringLiteral("user")), QStringLiteral("carol"));
}

void SecretStoreTest::aBrowserLoginIsRecognisedAsOne() {
    // Keyring entries do not say what they are; the kind is read off the
    // attributes, and it decides where the entry goes on the far side.
    const QString realm = QStringLiteral("https://transmit-test.example.org");

    platform::SecretRecord written;
    written.kind = platform::SecretKind::BrowserLogin;
    written.service = realm;
    written.account = QStringLiteral("bob");
    written.label = QStringLiteral("Test browser login");
    written.secret = QStringLiteral("swordfish");
    written.attributes.insert(QStringLiteral("signon_realm"), realm);
    written.attributes.insert(QStringLiteral("username_value"), QStringLiteral("bob"));

    QCOMPARE(store_->store(written).outcome, platform::ApplyOutcome::Applied);

    const platform::SecretRecord found = findByService(realm);
    QVERIFY(!found.secret.isEmpty());
    QCOMPARE(found.kind, platform::SecretKind::BrowserLogin);
}

QTEST_MAIN(SecretStoreTest)
#include "SecretStoreTest.moc"
