// What may and may not be unmounted.
//
// Ejecting is a convenience right up until it unmounts the wrong thing, and
// then it is somebody's home directory going away while they are working in
// it. The unmount itself is the operating system's and cannot be exercised
// here - there is no removable drive on a build machine, and unmounting a real
// one to find out would be a poor way to learn. What can be exercised, and is
// the part that decides whether the wrong volume is ever reached, is which
// paths get as far as the unmount at all.

#include <QTest>

#include <memory>
#include <utility>

#include "platform/PlatformService.h"

#include "FakePlatform.h"

using namespace transmit;

namespace {

platform::StorageVolume driveAt(const QString& root, const QString& name, bool removable) {
    platform::StorageVolume volume;
    volume.displayName = name;
    volume.rootPath = root;
    volume.fileSystem = QStringLiteral("vfat");
    volume.totalBytes = 64ULL << 30;
    volume.freeBytes = 32ULL << 30;
    volume.removable = removable;
    return volume;
}

/// Records whether the unmount was reached, so a test can tell "refused" from
/// "tried and the system said no" - which read the same from the outside and
/// are opposite answers to the question these tests ask.
class CountingPlatform : public testing::PlatformWithDrives {
public:
    using PlatformWithDrives::PlatformWithDrives;

    [[nodiscard]] int unmountsAttempted() const { return attempts_; }
    [[nodiscard]] QString lastPath() const { return lastPath_; }

protected:
    [[nodiscard]] QString unmountVolume(const QString& rootPath) const override {
        ++attempts_;
        lastPath_ = rootPath;
        return {};  // as though the system had done it
    }

private:
    mutable int attempts_ = 0;
    mutable QString lastPath_;
};

}  // namespace

class EjectTest : public QObject {
    Q_OBJECT

private slots:
    void aRemovableDriveIsEjected();
    void aDriveTheSystemDoesNotListIsRefused();
    void aFixedDriveIsRefused();
    void nothingAtAllIsRefused();
    void aPathInsideTheDriveIsRefused();
    void aPlatformWithNoWayToDoItSaysSo();
};

void EjectTest::aRemovableDriveIsEjected() {
    CountingPlatform platform(
        platform::PlatformService::create(),
        {driveAt(QStringLiteral("/media/usb"), QStringLiteral("Stick"), true)});

    QCOMPARE(platform.eject(QStringLiteral("/media/usb")), QString());
    QCOMPARE(platform.unmountsAttempted(), 1);
    QCOMPARE(platform.lastPath(), QStringLiteral("/media/usb"));
}

void EjectTest::aDriveTheSystemDoesNotListIsRefused() {
    CountingPlatform platform(
        platform::PlatformService::create(),
        {driveAt(QStringLiteral("/media/usb"), QStringLiteral("Stick"), true)});

    // The one that matters. A path nobody listed is a path nobody checked, and
    // unmounting it is unmounting whatever happens to be there.
    const QString refused = platform.eject(QStringLiteral("/home/ada"));
    QVERIFY2(!refused.isEmpty(), "ejected a path the platform never listed");
    QCOMPARE(platform.unmountsAttempted(), 0);
}

void EjectTest::aFixedDriveIsRefused() {
    CountingPlatform platform(
        platform::PlatformService::create(),
        {driveAt(QStringLiteral("/"), QStringLiteral("Macintosh HD"), false)});

    const QString refused = platform.eject(QStringLiteral("/"));
    QVERIFY2(!refused.isEmpty(), "ejected the system volume");
    QVERIFY2(refused.contains(QStringLiteral("Macintosh HD")), qPrintable(refused));
    QCOMPARE(platform.unmountsAttempted(), 0);
}

void EjectTest::nothingAtAllIsRefused() {
    CountingPlatform platform(platform::PlatformService::create(), {});
    QVERIFY(!platform.eject(QString()).isEmpty());
    QCOMPARE(platform.unmountsAttempted(), 0);
}

void EjectTest::aPathInsideTheDriveIsRefused() {
    CountingPlatform platform(
        platform::PlatformService::create(),
        {driveAt(QStringLiteral("/media/usb"), QStringLiteral("Stick"), true)});

    // The archive's own folder, not the drive's root. Unmounting takes the
    // whole volume, so a path that merely sits on one is not the same request
    // and is not treated as one.
    QVERIFY(!platform.eject(QStringLiteral("/media/usb/backups")).isEmpty());
    QCOMPARE(platform.unmountsAttempted(), 0);
}

void EjectTest::aPlatformWithNoWayToDoItSaysSo() {
    // The decorator does not override the unmount, so this is the base class's
    // answer: a platform that cannot do this is not obliged to pretend, and
    // what it returns has to be a sentence somebody can act on.
    testing::PlatformWithDrives platform(
        platform::PlatformService::create(),
        {driveAt(QStringLiteral("/media/usb"), QStringLiteral("Stick"), true)});

    const QString answer = platform.eject(QStringLiteral("/media/usb"));

    // On a platform that does implement it, this is a real attempt against a
    // path that is not mounted, which fails too. Either way it must not claim
    // to have worked.
    QVERIFY2(!answer.isEmpty(), "claimed to have ejected a drive that is not there");
}

QTEST_MAIN(EjectTest)
#include "EjectTest.moc"
