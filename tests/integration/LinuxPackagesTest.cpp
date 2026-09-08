#include <QTest>

#include "platform/linux/LinuxPlatformService.h"

namespace transmit::platform {

/// What a package listing means.
///
/// Nine package managers, one function each, and until this file every one of
/// them could only be checked on a machine that had that package manager - so
/// eight were checked nowhere at all. Getting one of these wrong does not
/// crash anything: it puts something that is not a program into the list of
/// things to reinstall, or leaves a program out of it, and nobody finds out
/// until the far side.
///
/// The samples are the shape of real output, header rows and all.
class LinuxPackagesTest : public QObject {
    Q_OBJECT

private slots:
    void readsWhatDpkgSays();
    void readsWhatRpmSays();
    void readsWhatPacmanSays();
    void readsWhatPortageSays();
    void readsWhatNixSays();
    void readsWhatApkSays();
    void readsWhatXbpsSays();
    void readsWhatFlatpakSays();
    void readsWhatSnapSaysPastItsHeader();
    void aLineThatIsNotAPackageIsNotAPackage();
    void everyPackageManagerHasAWayToBeAsked();

private:
    /// The names in a listing, in order, so a case can say what it means in
    /// one line.
    [[nodiscard]] static QStringList namesIn(const QString& output, PackageSource source) {
        QStringList names;
        for (const InstalledApp& app : packagesFromListing(output, source)) {
            names << app.id;
        }
        return names;
    }
};

void LinuxPackagesTest::readsWhatDpkgSays() {
    const QString output = QStringLiteral(
        "adduser\t3.137ubuntu1\n"
        "apt\t2.7.14build2\n"
        "base-files\t13ubuntu10.2\n");

    const QList<InstalledApp> apps = packagesFromListing(output, PackageSource::Apt);
    QCOMPARE(apps.size(), 3);
    QCOMPARE(apps[0].id, QStringLiteral("adduser"));
    QCOMPARE(apps[0].displayName, QStringLiteral("adduser"));
    QCOMPARE(apps[0].version, QStringLiteral("3.137ubuntu1"));
    QCOMPARE(apps[0].source, PackageSource::Apt);
    QCOMPARE(apps[2].version, QStringLiteral("13ubuntu10.2"));

    // A package whose version dpkg does not know is still a package.
    QCOMPARE(namesIn(QStringLiteral("halfinstalled\t\n"), PackageSource::Apt),
             QStringList{QStringLiteral("halfinstalled")});
}

void LinuxPackagesTest::readsWhatRpmSays() {
    const QString output = QStringLiteral(
        "bash\t5.2.26-3.fc40\n"
        "kernel-core\t6.8.5-301.fc40\n");

    const QList<InstalledApp> apps = packagesFromListing(output, PackageSource::Dnf);
    QCOMPARE(apps.size(), 2);
    QCOMPARE(apps[1].id, QStringLiteral("kernel-core"));
    QCOMPARE(apps[1].version, QStringLiteral("6.8.5-301.fc40"));

    // openSUSE asks rpm the same way and gets the same answer, and each is
    // recorded as having come from its own package manager.
    QCOMPARE(packagesFromListing(output, PackageSource::Zypper).at(0).source,
             PackageSource::Zypper);
}

void LinuxPackagesTest::readsWhatPacmanSays() {
    const QString output = QStringLiteral(
        "base 3-2\n"
        "firefox 128.0-1\n"
        "linux-firmware 20240513.7f0d719b-1\n");

    const QList<InstalledApp> apps = packagesFromListing(output, PackageSource::Pacman);
    QCOMPARE(apps.size(), 3);
    QCOMPARE(apps[2].id, QStringLiteral("linux-firmware"));
    QCOMPARE(apps[2].version, QStringLiteral("20240513.7f0d719b-1"));
}

void LinuxPackagesTest::readsWhatPortageSays() {
    // qlist prints category/name-version as one word. Splitting it would have
    // to know which of the dashes belongs to the name, so it is kept whole -
    // which is also exactly what portage takes back.
    const QString output = QStringLiteral(
        "app-editors/vim-9.1.0366\n"
        "sys-apps/systemd-255.6\n");

    const QList<InstalledApp> apps = packagesFromListing(output, PackageSource::Portage);
    QCOMPARE(apps.size(), 2);
    QCOMPARE(apps[0].id, QStringLiteral("app-editors/vim-9.1.0366"));
    QVERIFY(apps[0].version.isEmpty());
}

void LinuxPackagesTest::readsWhatNixSays() {
    // The listing prints several lines per package and only one of them names
    // anything. Taking the last word of every line - which is what this used
    // to do - made a package called "0" out of the index.
    const QString modern = QStringLiteral(
        "Index:              0\n"
        "Flake attribute:    legacyPackages.x86_64-linux.hello\n"
        "Original flake URL: flake:nixpkgs\n"
        "Locked flake URL:   github:NixOS/nixpkgs/9e0e0b3\n"
        "Store paths:        /nix/store/abc123-hello-2.12.1\n"
        "\n"
        "Index:              1\n"
        "Flake attribute:    legacyPackages.x86_64-linux.ripgrep\n"
        "Store paths:        /nix/store/def456-ripgrep-14.1.0\n");

    QCOMPARE(namesIn(modern, PackageSource::Nix),
             (QStringList{QStringLiteral("/nix/store/abc123-hello-2.12.1"),
                          QStringLiteral("/nix/store/def456-ripgrep-14.1.0")}));

    // And the one-line-per-package shape the older releases print.
    const QString older = QStringLiteral(
        "0 flake:nixpkgs#legacyPackages.x86_64-linux.hello "
        "github:NixOS/nixpkgs/9e0e0b3#legacyPackages.x86_64-linux.hello "
        "/nix/store/abc123-hello-2.12.1\n");
    QCOMPARE(namesIn(older, PackageSource::Nix),
             QStringList{QStringLiteral("/nix/store/abc123-hello-2.12.1")});
}

void LinuxPackagesTest::readsWhatApkSays() {
    const QString output = QStringLiteral(
        "alpine-baselayout\n"
        "busybox\n"
        "musl\n");
    QCOMPARE(namesIn(output, PackageSource::Apk),
             (QStringList{QStringLiteral("alpine-baselayout"), QStringLiteral("busybox"),
                          QStringLiteral("musl")}));
}

void LinuxPackagesTest::readsWhatXbpsSays() {
    // bash-5.2.15_1: the version is the tail with no dash in it, so every dash
    // before that one belongs to the name.
    const QString output = QStringLiteral(
        "bash-5.2.15_1\n"
        "xbps-triggers-0.123_1\n");

    const QList<InstalledApp> apps = packagesFromListing(output, PackageSource::Xbps);
    QCOMPARE(apps.size(), 2);
    QCOMPARE(apps[0].id, QStringLiteral("bash"));
    QCOMPARE(apps[0].version, QStringLiteral("5.2.15_1"));
    QCOMPARE(apps[1].id, QStringLiteral("xbps-triggers"));
    QCOMPARE(apps[1].version, QStringLiteral("0.123_1"));
}

void LinuxPackagesTest::readsWhatFlatpakSays() {
    const QString output = QStringLiteral(
        "org.mozilla.firefox\t128.0\n"
        "com.valvesoftware.Steam\t1.0.0.79\n"
        "io.gitlab.librewolf\t\n");

    const QList<InstalledApp> apps = packagesFromListing(output, PackageSource::Flatpak);
    QCOMPARE(apps.size(), 3);
    QCOMPARE(apps[0].id, QStringLiteral("org.mozilla.firefox"));
    QCOMPARE(apps[0].version, QStringLiteral("128.0"));
    QCOMPARE(apps[2].id, QStringLiteral("io.gitlab.librewolf"));
    QVERIFY(apps[2].version.isEmpty());
}

void LinuxPackagesTest::readsWhatSnapSaysPastItsHeader() {
    // The header is a row of column names, and reading it as a package puts
    // something called "Name" in the list of things to reinstall.
    const QString output = QStringLiteral(
        "Name               Version    Rev    Tracking       Publisher   Notes\n"
        "core22             20240111   1122   latest/stable  canonical*  base\n"
        "firefox            128.0-2    4173   latest/stable  mozilla*    -\n");

    const QList<InstalledApp> apps = packagesFromListing(output, PackageSource::Snap);
    QCOMPARE(apps.size(), 2);
    QCOMPARE(apps[0].id, QStringLiteral("core22"));
    QCOMPARE(apps[0].version, QStringLiteral("20240111"));
    QCOMPARE(apps[1].id, QStringLiteral("firefox"));
    QCOMPARE(apps[1].version, QStringLiteral("128.0-2"));
}

void LinuxPackagesTest::aLineThatIsNotAPackageIsNotAPackage() {
    // Nothing at all, and nothing but blank lines.
    QVERIFY(packagesFromListing(QString(), PackageSource::Apt).isEmpty());
    QVERIFY(packagesFromListing(QStringLiteral("\n\n\n"), PackageSource::Apt).isEmpty());

    // A listing whose lines do not fit the rule. Inventing an entry from one
    // of these puts a package that does not exist into the install script.
    QVERIFY(
        packagesFromListing(QStringLiteral("dpkg-query: no packages found\n"), PackageSource::Apt)
            .isEmpty());

    // A source with no way to be asked answers with nothing rather than
    // guessing at a format it does not have.
    QVERIFY(packagesFromListing(QStringLiteral("anything\n"), PackageSource::Slackware).isEmpty());
    QVERIFY(packagesFromListing(QStringLiteral("anything\n"), PackageSource::Unknown).isEmpty());

    // A snap listing of nothing but its header is a machine with no snaps.
    QVERIFY(
        packagesFromListing(QStringLiteral("Name  Version  Rev\n"), PackageSource::Snap).isEmpty());
}

void LinuxPackagesTest::everyPackageManagerHasAWayToBeAsked() {
    // A distribution Transmit claims to support and cannot ask is one whose
    // users get an empty list and no explanation.
    for (const PackageSource source :
         {PackageSource::Apt, PackageSource::Dnf, PackageSource::Zypper, PackageSource::Pacman,
          PackageSource::Portage, PackageSource::Nix, PackageSource::Apk, PackageSource::Xbps,
          PackageSource::Flatpak, PackageSource::Snap}) {
        const auto query = packageQueryFor(source);
        QVERIFY2(query.has_value(), qPrintable(packageSourceName(source)));
        QVERIFY2(!query->program.isEmpty(), qPrintable(packageSourceName(source)));
        QVERIFY2(!query->pattern.isEmpty(), qPrintable(packageSourceName(source)));

        const QRegularExpression pattern(query->pattern);
        QVERIFY2(pattern.isValid(), qPrintable(packageSourceName(source) + QStringLiteral(": ") +
                                               pattern.errorString()));
    }

    // And the two that genuinely have no command say so rather than being
    // given one that does not exist.
    QVERIFY(!packageQueryFor(PackageSource::Slackware).has_value());
    QVERIFY(!packageQueryFor(PackageSource::AppImage).has_value());
}

}  // namespace transmit::platform

QTEST_GUILESS_MAIN(transmit::platform::LinuxPackagesTest)
#include "LinuxPackagesTest.moc"
