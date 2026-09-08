#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "core/secrets/SecretsDomain.h"
#include "format/Serialization.h"
#include "platform/PlatformService.h"

#include "FakePlatform.h"

namespace transmit::core {
namespace {

using platform::ApplyOutcome;
using platform::ApplyResult;
using platform::SecretKind;
using platform::SecretRecord;

/// What a fake store was asked to do and what it did, kept where both the
/// store the platform handed out and the test that made it can see it.
struct Vault {
    bool available = true;
    QString name = QStringLiteral("a keyring that is not real");

    QList<SecretRecord> toRead;
    QList<SecretRecord> stored;           ///< everything store() was given
    QHash<QString, ApplyResult> answers;  ///< by label; anything else is Applied
    int readCalls = 0;
};

class FakeSecretStore final : public platform::SecretStore {
public:
    explicit FakeSecretStore(Vault& vault) : vault_(vault) {}

    [[nodiscard]] bool isAvailable() const override { return vault_.available; }
    [[nodiscard]] QString describe() const override { return vault_.name; }

    [[nodiscard]] QList<SecretRecord> read(bool includeWifi,
                                           bool includeApplications) const override {
        ++vault_.readCalls;
        QList<SecretRecord> wanted;
        for (const SecretRecord& record : vault_.toRead) {
            const bool wifi = record.kind == SecretKind::WifiNetwork;
            if ((wifi && includeWifi) || (!wifi && includeApplications)) {
                wanted.push_back(record);
            }
        }
        return wanted;
    }

    [[nodiscard]] ApplyResult store(const SecretRecord& record) const override {
        vault_.stored.push_back(record);
        return vault_.answers.value(record.label, ApplyResult{ApplyOutcome::Applied, {}, {}});
    }

private:
    Vault& vault_;
};

SecretRecord aPassword(const QString& label, const QString& secret,
                       SecretKind kind = SecretKind::ApplicationPassword) {
    SecretRecord record;
    record.kind = kind;
    record.service = label + QStringLiteral(".example");
    record.account = QStringLiteral("bob");
    record.secret = secret;
    record.label = label;
    return record;
}

/// Whether any note says something matching, so a case can check what the
/// person is told without pinning the whole sentence.
bool anyNoteMentions(const QList<ContinuityNote>& notes, const QString& fragment) {
    for (const ContinuityNote& note : notes) {
        if (note.subject.contains(fragment) || note.detail.contains(fragment)) {
            return true;
        }
    }
    return false;
}

}  // namespace

/// The one part of Transmit that handles material somebody would be harmed by
/// losing control of - and until this file, three per cent of it had ever been
/// run by a test. It could not be: reaching it needed a real credential store,
/// which needs a session bus, an unlocked keyring and a machine that has one.
///
/// So the store is a fake and everything above it is real. What these check is
/// the part that is the same on every system: what is carried, what is left
/// behind, what the person is told, and - the one that matters most - what
/// never reaches the disk.
class SecretsDomainTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void carriesWhatTheStoreWillGiveAndSaysWhatItHolds();
    void takesOnlyTheKindsThatWereAskedFor();
    void aPasswordTheSystemWillNotRevealIsReportedRatherThanDropped();
    void aSystemWithNoStoreCarriesNothingAndSaysSo();
    void whatWasCarriedComesBackTheSameOnTheOtherSide();
    void aDryRunAddsNothingToTheStore();
    void anArchiveWithNoCredentialsSaysNothingAtAll();
    void rubbishInThePayloadIsNotCredentials();
    void aRecordWithNoServiceIsNotACredential();
    void aFieldFromALaterVersionIsSteppedOver();
    void whatCouldNotBeStoredIsNamed();
    void theScriptForWhatNeedsRightsNeverHoldsAPassword();
    void nothingIsWrittenWhenThereIsNowhereToWriteIt();

private:
    [[nodiscard]] std::unique_ptr<testing::PlatformWithDrives> platformWith(Vault& vault) const;

    QTemporaryDir workspace_;
};

void SecretsDomainTest::init() {}
void SecretsDomainTest::cleanup() {}

std::unique_ptr<testing::PlatformWithDrives> SecretsDomainTest::platformWith(Vault& vault) const {
    auto fake = std::make_unique<testing::PlatformWithDrives>(platform::PlatformService::create(),
                                                              QList<platform::StorageVolume>{});
    fake->keepCredentialsIn(
        [&vault] { return std::unique_ptr<platform::SecretStore>(new FakeSecretStore(vault)); });
    return fake;
}

void SecretsDomainTest::carriesWhatTheStoreWillGiveAndSaysWhatItHolds() {
    Vault vault;
    vault.toRead = {aPassword(QStringLiteral("mail"), QStringLiteral("hunter2")),
                    aPassword(QStringLiteral("home wifi"), QStringLiteral("correct horse"),
                              SecretKind::WifiNetwork)};

    const auto platform = platformWith(vault);
    const SecretsDomain domain(*platform);
    QVERIFY(domain.isAvailable());
    QCOMPARE(domain.describeStore(), vault.name);

    const SecretsDomain::CaptureResult captured = domain.capture({});
    QCOMPARE(captured.captured, 2);
    QCOMPARE(captured.unreadable, 0);
    QVERIFY(!captured.payload.empty());

    // The person is told plainly that the drive now holds passwords, and which
    // store they came from. This is the whole reason the feature is opt-in.
    QVERIFY(anyNoteMentions(captured.notes, vault.name));
    QVERIFY(anyNoteMentions(captured.notes, QStringLiteral("keep the drive and the passphrase")));
}

void SecretsDomainTest::takesOnlyTheKindsThatWereAskedFor() {
    Vault vault;
    vault.toRead = {
        aPassword(QStringLiteral("mail"), QStringLiteral("hunter2")),
        aPassword(QStringLiteral("cafe wifi"), QStringLiteral("latte"), SecretKind::WifiNetwork)};
    const auto platform = platformWith(vault);
    const SecretsDomain domain(*platform);

    QCOMPARE(domain.capture({true, false}).captured, 1);
    QCOMPARE(domain.capture({false, true}).captured, 1);
    QCOMPARE(domain.capture({false, false}).captured, 0);
    QCOMPARE(vault.readCalls, 3);
}

void SecretsDomainTest::aPasswordTheSystemWillNotRevealIsReportedRatherThanDropped() {
    Vault vault;
    vault.toRead = {aPassword(QStringLiteral("mail"), QStringLiteral("hunter2")),
                    // Found, named, and refused: the system would not hand the
                    // value over without rights Transmit does not ask for.
                    aPassword(QStringLiteral("company vpn"), QString())};

    const auto platform = platformWith(vault);
    const SecretsDomain domain(*platform);
    const SecretsDomain::CaptureResult captured = domain.capture({});

    QCOMPARE(captured.captured, 1);
    QCOMPARE(captured.unreadable, 1);
    QVERIFY2(anyNoteMentions(captured.notes, QStringLiteral("company vpn")),
             "a password that could not be read has to be named, or nobody knows to re-enter it");
}

void SecretsDomainTest::aSystemWithNoStoreCarriesNothingAndSaysSo() {
    Vault vault;
    vault.available = false;
    vault.toRead = {aPassword(QStringLiteral("mail"), QStringLiteral("hunter2"))};

    const auto platform = platformWith(vault);
    const SecretsDomain domain(*platform);
    QVERIFY(!domain.isAvailable());

    const SecretsDomain::CaptureResult captured = domain.capture({});
    QCOMPARE(captured.captured, 0);
    QVERIFY(captured.payload.empty());
    QCOMPARE(vault.readCalls, 0);
    QCOMPARE(captured.notes.size(), 1);
    QCOMPARE(captured.notes.front().grade, ContinuityGrade::Impossible);
}

void SecretsDomainTest::whatWasCarriedComesBackTheSameOnTheOtherSide() {
    Vault source;
    source.toRead = {aPassword(QStringLiteral("mail"), QStringLiteral("hunter2")),
                     aPassword(QStringLiteral("home wifi"), QStringLiteral("correct horse"),
                               SecretKind::WifiNetwork)};
    // A password with the awkward characters a real one has in it.
    source.toRead.push_back(
        aPassword(QStringLiteral("nas"), QStringLiteral("päss wörd \"'\\§$%&/()=?")));

    const auto from = platformWith(source);
    const SecretsDomain::CaptureResult captured = SecretsDomain(*from).capture({});
    QCOMPARE(captured.captured, 3);

    Vault target;
    const auto to = platformWith(target);
    const QList<ContinuityNote> notes = SecretsDomain(*to).restore(
        format::ByteView(captured.payload), workspace_.filePath("scripts"), false);

    QCOMPARE(target.stored.size(), 3);
    for (const SecretRecord& original : source.toRead) {
        bool found = false;
        for (const SecretRecord& arrived : target.stored) {
            if (arrived.label != original.label) {
                continue;
            }
            found = true;
            QCOMPARE(arrived.secret, original.secret);
            QCOMPARE(arrived.service, original.service);
            QCOMPARE(arrived.account, original.account);
            QCOMPARE(arrived.kind, original.kind);
        }
        QVERIFY2(found, qPrintable(original.label));
    }
    QVERIFY(anyNoteMentions(notes, target.name));
}

void SecretsDomainTest::aDryRunAddsNothingToTheStore() {
    Vault source;
    source.toRead = {aPassword(QStringLiteral("mail"), QStringLiteral("hunter2"))};
    const auto from = platformWith(source);
    const SecretsDomain::CaptureResult captured = SecretsDomain(*from).capture({});

    Vault target;
    const auto to = platformWith(target);
    const QList<ContinuityNote> notes = SecretsDomain(*to).restore(
        format::ByteView(captured.payload), workspace_.filePath("scripts"), true);

    QVERIFY2(target.stored.isEmpty(), "a dry run that writes a password into a keyring is not one");
    QCOMPARE(notes.size(), 1);
    QCOMPARE(notes.front().grade, ContinuityGrade::Adapted);
    QVERIFY(anyNoteMentions(notes, QStringLiteral("would be added")));
}

void SecretsDomainTest::anArchiveWithNoCredentialsSaysNothingAtAll() {
    Vault target;
    const auto to = platformWith(target);
    QVERIFY(SecretsDomain(*to)
                .restore(format::ByteView{}, workspace_.filePath("scripts"), false)
                .isEmpty());
    QVERIFY(target.stored.isEmpty());
}

void SecretsDomainTest::rubbishInThePayloadIsNotCredentials() {
    // Whatever is in an archive got there from somewhere, and the reader is
    // the one thing between it and this machine's keyring. Nothing here may
    // become a credential, and nothing may run off the end of the buffer -
    // which the sanitiser build is what actually checks.
    const std::string junk[] = {"\x01", "\xff\xff\xff\xff", "\x0a\x05hello",
                                std::string("\x00\x00\x00", 3), "not a manifest at all"};

    Vault target;
    const auto to = platformWith(target);
    for (const std::string& bytes : junk) {
        const auto* start = reinterpret_cast<const format::Byte*>(bytes.data());
        const QList<ContinuityNote> notes = SecretsDomain(*to).restore(
            format::ByteView(start, bytes.size()), workspace_.filePath("scripts"), false);
        QVERIFY(notes.isEmpty());
    }
    QVERIFY(target.stored.isEmpty());
}

void SecretsDomainTest::aRecordWithNoServiceIsNotACredential() {
    // A record naming nothing cannot be stored anywhere, and letting one
    // through would put an entry with an empty service into the keyring.
    format::ByteBuffer payload;
    format::ByteWriter writer(payload);
    writer.putRecord(1, [](format::ByteWriter& nested) {
        nested.putUInt(1, 0);
        nested.putString(3, "bob");
        nested.putString(4, "hunter2");
    });

    Vault target;
    const auto to = platformWith(target);
    QVERIFY(SecretsDomain(*to)
                .restore(format::ByteView(payload), workspace_.filePath("scripts"), false)
                .isEmpty());
    QVERIFY(target.stored.isEmpty());
}

void SecretsDomainTest::aFieldFromALaterVersionIsSteppedOver() {
    // A newer Transmit adding a field to a record must not stop an older one
    // reading the rest of it, which is the whole point of the tagged format.
    format::ByteBuffer payload;
    format::ByteWriter writer(payload);
    writer.putRecord(1, [](format::ByteWriter& nested) {
        nested.putUInt(1, 0);
        nested.putString(2, "mail.example");
        nested.putString(3, "bob");
        nested.putString(4, "hunter2");
        nested.putString(5, "mail");
        nested.putString(97, "something a later version knows about");
        nested.putUInt(98, 12345);
    });
    // And a whole record of a kind this version has never heard of.
    writer.putString(96, "not a credential");

    Vault target;
    const auto to = platformWith(target);
    const QList<ContinuityNote> notes = SecretsDomain(*to).restore(
        format::ByteView(payload), workspace_.filePath("scripts"), false);

    QCOMPARE(target.stored.size(), 1);
    QCOMPARE(target.stored.front().secret, QStringLiteral("hunter2"));
    QCOMPARE(target.stored.front().label, QStringLiteral("mail"));
    QVERIFY(!notes.isEmpty());
}

void SecretsDomainTest::whatCouldNotBeStoredIsNamed() {
    Vault source;
    source.toRead = {aPassword(QStringLiteral("mail"), QStringLiteral("hunter2")),
                     aPassword(QStringLiteral("nas"), QStringLiteral("swordfish")),
                     aPassword(QStringLiteral("old thing"), QStringLiteral("letmein"))};
    const auto from = platformWith(source);
    const SecretsDomain::CaptureResult captured = SecretsDomain(*from).capture({});

    Vault target;
    target.answers[QStringLiteral("nas")] = ApplyResult{ApplyOutcome::Failed, {}, {}};
    target.answers[QStringLiteral("old thing")] = ApplyResult{ApplyOutcome::Unsupported, {}, {}};
    const auto to = platformWith(target);

    const QList<ContinuityNote> notes = SecretsDomain(*to).restore(
        format::ByteView(captured.payload), workspace_.filePath("scripts"), false);

    QVERIFY2(anyNoteMentions(notes, QStringLiteral("nas")),
             "a password that did not arrive has "
             "to be named");
    QVERIFY(anyNoteMentions(notes, QStringLiteral("old thing")));
    QVERIFY(anyNoteMentions(notes, QStringLiteral("enter these again")));
}

void SecretsDomainTest::theScriptForWhatNeedsRightsNeverHoldsAPassword() {
    const QString passphrase = QStringLiteral("correct-horse-battery-staple");

    Vault source;
    source.toRead = {aPassword(QStringLiteral("home wifi"), passphrase, SecretKind::WifiNetwork)};
    const auto from = platformWith(source);
    const SecretsDomain::CaptureResult captured = SecretsDomain(*from).capture({});

    Vault target;
    target.answers[QStringLiteral("home wifi")] =
        ApplyResult{ApplyOutcome::NeedsPrivilege,
                    {},
                    QStringLiteral("nmcli device wifi connect 'home wifi' --ask")};
    const auto to = platformWith(target);

    const QString directory = workspace_.filePath("scripts");
    const QList<ContinuityNote> notes =
        SecretsDomain(*to).restore(format::ByteView(captured.payload), directory, false);

    QVERIFY(anyNoteMentions(notes, QStringLiteral("home wifi")));

    QDir folder(directory);
    const QStringList written = folder.entryList(QDir::Files);
    QCOMPARE(written.size(), 1);

    QFile script(folder.filePath(written.front()));
    QVERIFY(script.open(QIODevice::ReadOnly));
    const QByteArray content = script.readAll();

    // The rule this whole file exists to hold: a password never reaches the
    // disk outside the encrypted archive. The script says how to add the
    // network and the command asks for the password when it is run.
    QVERIFY2(!content.contains(passphrase.toUtf8()),
             "the passphrase was written into a plain file beside the archive");
    QVERIFY(content.contains("nmcli device wifi connect"));
    QVERIFY(content.contains("NOT written in this file"));

#ifndef Q_OS_WIN
    // And nobody else on the machine may read it either.
    const QFile::Permissions permissions = QFile(script.fileName()).permissions();
    QVERIFY(!permissions.testFlag(QFile::ReadGroup));
    QVERIFY(!permissions.testFlag(QFile::ReadOther));
#endif
}

void SecretsDomainTest::nothingIsWrittenWhenThereIsNowhereToWriteIt() {
    Vault source;
    source.toRead = {
        aPassword(QStringLiteral("home wifi"), QStringLiteral("latte"), SecretKind::WifiNetwork)};
    const auto from = platformWith(source);
    const SecretsDomain::CaptureResult captured = SecretsDomain(*from).capture({});

    Vault target;
    target.answers[QStringLiteral("home wifi")] = ApplyResult{
        ApplyOutcome::NeedsPrivilege, {}, QStringLiteral("nmcli device wifi connect --ask")};
    const auto to = platformWith(target);

    // No directory to put a script in. The person still has to be told which
    // networks they have to join themselves, by name.
    const QList<ContinuityNote> notes =
        SecretsDomain(*to).restore(format::ByteView(captured.payload), QString(), false);
    QVERIFY(anyNoteMentions(notes, QStringLiteral("home wifi")));
    QVERIFY(anyNoteMentions(notes, QStringLiteral("administrator rights")));
}

}  // namespace transmit::core

QTEST_GUILESS_MAIN(transmit::core::SecretsDomainTest)
#include "SecretsDomainTest.moc"
