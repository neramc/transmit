// Files whose contents are not on this machine.
//
// OneDrive and iCloud Drive both leave a full-looking home directory behind:
// the names are there, the sizes are right, the modification times are right,
// and the bytes are on somebody else's disk. A scan cannot tell the difference
// without looking, and reading one to find out is exactly the thing that costs
// - it fetches the file. Get this wrong and a capture does not fail; it
// silently downloads a few hundred gigabytes over whatever connection the
// machine happens to be on, which is the kind of bug that arrives as a phone
// bill.
//
// Which evidence counts differs by system, so the system is a parameter. That
// is what lets the macOS rule be run here, on a Linux runner, against a fixture
// - rather than only on the one machine that has iCloud Drive.

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "core/continuity/ContinuityTypes.h"
#include "core/services/ScanService.h"
#include "format/FileTraits.h"
#include "platform/PlatformService.h"

#include "FakePlatform.h"

using namespace transmit;

class CloudFilesTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void theRuleIsReadFromTheRightEvidence();
    void anEvictedFileIsListedRatherThanFetched();
    void theSameNameIsAnOrdinaryFileOnEveryOtherSystem();
    void askingForThemFetchesThem();
    void aPolicySetForTheWholeCaptureSurvivesARootThatSaysNothing();

private:
    /// A scan of the fixture as though this were `os`.
    [[nodiscard]] core::ScanResult scanAs(format::OsFamily os, bool fetchCloudFiles = false) const;

    /// A scan of the fixture with one scope rule set on the selection and
    /// nothing at all on the root, which is what every ordinary user folder
    /// looks like.
    [[nodiscard]] core::ScanResult scanFollowingLinks(bool follow) const;

    [[nodiscard]] core::CaptureSelection selectionOfTheFixture() const;

    /// True when this platform builds its folder table from the environment,
    /// so the fake home below is the one the scan actually reads.
    [[nodiscard]] bool homeIsHonoured() const;

    std::unique_ptr<QTemporaryDir> workspace_;
    QString home_;
    QString originalHome_;
};

void CloudFilesTest::initTestCase() {
    workspace_ = std::make_unique<QTemporaryDir>();
    QVERIFY(workspace_->isValid());

    home_ = workspace_->filePath(QStringLiteral("home"));
    QVERIFY(QDir().mkpath(home_ + QStringLiteral("/Documents")));

    originalHome_ = qEnvironmentVariable("HOME");
    qputenv("HOME", home_.toUtf8());
    qputenv("XDG_DOCUMENTS_DIR", (home_ + QStringLiteral("/Documents")).toUtf8());

    // One ordinary file, and one stub of the shape iCloud Drive leaves behind
    // when it evicts `quarterly.pdf`. The stub really is a small file - a few
    // hundred bytes of metadata - which is why its size cannot be the test.
    const auto write = [](const QString& path, const QByteArray& contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), static_cast<qint64>(contents.size()));
        file.close();
    };
    write(home_ + QStringLiteral("/Documents/here.txt"), QByteArray(4096, 'a'));
    write(home_ + QStringLiteral("/Documents/.quarterly.pdf.icloud"), QByteArray(320, 'i'));

    // A folder outside Documents, and a link to it from inside. Following the
    // link is the only way the file in it is reached.
    QVERIFY(QDir().mkpath(home_ + QStringLiteral("/elsewhere")));
    write(home_ + QStringLiteral("/elsewhere/beyond.txt"), QByteArray(64, 'b'));
    QVERIFY(QFile::link(home_ + QStringLiteral("/elsewhere"),
                        home_ + QStringLiteral("/Documents/linked")));
}

void CloudFilesTest::cleanupTestCase() {
    if (originalHome_.isEmpty()) {
        qunsetenv("HOME");
    } else {
        qputenv("HOME", originalHome_.toUtf8());
    }
    qunsetenv("XDG_DOCUMENTS_DIR");
    workspace_.reset();
}

bool CloudFilesTest::homeIsHonoured() const {
    const format::PathTokenMap folders = platform::PlatformService::create()->knownFolders();
    const auto base = folders.base(format::PathTokenId::Documents);
    return base.has_value() && QString::fromStdString(*base).startsWith(home_);
}

core::CaptureSelection CloudFilesTest::selectionOfTheFixture() const {
    core::CaptureRoot root;
    root.token = format::PathTokenId::Documents;
    root.domain = format::DomainId::UserData;
    root.recursive = true;

    core::CaptureSelection selection;
    selection.roots = {root};
    return selection;
}

core::ScanResult CloudFilesTest::scanAs(format::OsFamily os, bool fetchCloudFiles) const {
    auto platform = std::make_unique<testing::PlatformWithDrives>(
        platform::PlatformService::create(), QList<platform::StorageVolume>{});
    platform->pretendToBe(os);

    core::CaptureSelection selection = selectionOfTheFixture();
    selection.scope.fetchCloudFiles = fetchCloudFiles;

    const core::ScanService scanner(*platform);
    core::CancelToken token;
    return scanner.scan(selection, token, {});
}

core::ScanResult CloudFilesTest::scanFollowingLinks(bool follow) const {
    auto platform = std::make_unique<testing::PlatformWithDrives>(
        platform::PlatformService::create(), QList<platform::StorageVolume>{});

    core::CaptureSelection selection = selectionOfTheFixture();
    selection.scope.followSymlinks = follow;

    const core::ScanService scanner(*platform);
    core::CancelToken token;
    return scanner.scan(selection, token, {});
}

void CloudFilesTest::theRuleIsReadFromTheRightEvidence() {
    // No filesystem involved: the decision itself, for each system, from the
    // two things it is allowed to look at.
    format::WindowsMetadata plain;
    format::WindowsMetadata online;
    online.attributes =
        format::file_attribute::kArchive | format::file_attribute::kRecallOnDataAccess;

    // Windows says it in the attribute word, and that answer holds wherever the
    // archive is read - a restore onto Linux must reach the same conclusion
    // about a file captured on Windows.
    for (const format::OsFamily os :
         {format::OsFamily::Windows, format::OsFamily::MacOs, format::OsFamily::Linux}) {
        QVERIFY(core::storedOnlyInTheCloud(QStringLiteral("quarterly.pdf"), online, os));
        QVERIFY(!core::storedOnlyInTheCloud(QStringLiteral("quarterly.pdf"), plain, os));
    }

    // The name rule is macOS's alone. Somewhere else it is just a file with an
    // odd name, and refusing to carry it would be a bug wearing a feature's
    // clothes.
    QVERIFY(core::storedOnlyInTheCloud(QStringLiteral(".quarterly.pdf.icloud"), plain,
                                       format::OsFamily::MacOs));
    QVERIFY(!core::storedOnlyInTheCloud(QStringLiteral(".quarterly.pdf.icloud"), plain,
                                        format::OsFamily::Linux));
    QVERIFY(!core::storedOnlyInTheCloud(QStringLiteral(".quarterly.pdf.icloud"), plain,
                                        format::OsFamily::Windows));
}

void CloudFilesTest::anEvictedFileIsListedRatherThanFetched() {
    if (!homeIsHonoured()) {
        QSKIP("this platform does not read its folder table from the environment");
    }

    const core::ScanResult scan = scanAs(format::OsFamily::MacOs);

    QCOMPARE(scan.fileCount, 1u);  // here.txt, and not the stub
    QCOMPARE(scan.skippedByReason.value(static_cast<int>(core::SkipReason::StoredInTheCloud)), 1u);

    // The size counted is the stub's own, which is what is on this disk. It is
    // reported so the person can see what fetching them would cost before
    // deciding to.
    QCOMPARE(scan.cloudOnlyBytes, 320u);

    for (const core::ScannedItem& item : scan.items) {
        QVERIFY2(!item.absolutePath.endsWith(QStringLiteral(".icloud")),
                 "the stub was captured as though it were the file");
    }

    // And it is said out loud rather than only counted.
    bool named = false;
    for (const core::ContinuityNote& note : scan.notes) {
        named = named || note.subject.endsWith(QStringLiteral(".quarterly.pdf.icloud"));
    }
    QVERIFY2(named, "nothing in the report says which file was left online");
}

void CloudFilesTest::theSameNameIsAnOrdinaryFileOnEveryOtherSystem() {
    if (!homeIsHonoured()) {
        QSKIP("this platform does not read its folder table from the environment");
    }

    for (const format::OsFamily os : {format::OsFamily::Linux, format::OsFamily::Windows}) {
        const core::ScanResult scan = scanAs(os);
        QCOMPARE(scan.fileCount, 2u);
        QCOMPARE(scan.skippedByReason.value(static_cast<int>(core::SkipReason::StoredInTheCloud)),
                 0u);
        QCOMPARE(scan.cloudOnlyBytes, 0u);
    }
}

void CloudFilesTest::askingForThemFetchesThem() {
    if (!homeIsHonoured()) {
        QSKIP("this platform does not read its folder table from the environment");
    }

    const core::ScanResult scan = scanAs(format::OsFamily::MacOs, /*fetchCloudFiles=*/true);
    QCOMPARE(scan.fileCount, 2u);
    QCOMPARE(scan.skippedByReason.value(static_cast<int>(core::SkipReason::StoredInTheCloud)), 0u);
}

void CloudFilesTest::aPolicySetForTheWholeCaptureSurvivesARootThatSaysNothing() {
    if (!homeIsHonoured()) {
        QSKIP("this platform does not read its folder table from the environment");
    }

    // Here because it is the same merge, and because this fixture is what
    // found it. A root may narrow what is taken - by size, by date, by
    // extension, by pattern - and every root that did not come from a
    // per-application choice carries a rule nobody wrote, built from the
    // defaults. Taking the stricter of the two for a flag whose default is
    // already the strict answer meant the root always won: `--follow-symlinks`
    // did nothing whatsoever on a user folder, and said nothing about it.
    const core::ScanResult left = scanFollowingLinks(false);
    const core::ScanResult followed = scanFollowingLinks(true);

    QCOMPARE(left.symlinkCount, 1u);
    QCOMPARE(left.fileCount, 2u);  // here.txt and the stub; nothing through the link

    QCOMPARE(followed.symlinkCount, 1u);
    QCOMPARE(followed.fileCount, 3u);

    bool reached = false;
    for (const core::ScannedItem& item : followed.items) {
        reached = reached || item.absolutePath.endsWith(QStringLiteral("beyond.txt"));
    }
    QVERIFY2(reached, "asking to follow links did not follow them");
}

QTEST_MAIN(CloudFilesTest)
#include "CloudFilesTest.moc"
