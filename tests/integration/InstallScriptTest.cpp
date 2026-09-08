#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "core/recipe/InstallScriptWriter.h"
#include "platform/PlatformService.h"

#include "FakePlatform.h"

namespace transmit::core {
namespace {

InventoryEntry app(const QString& name, const QHash<QString, QString>& ids) {
    InventoryEntry entry;
    entry.recipeId = name.toLower();
    entry.displayName = name;
    entry.installIds = ids;
    return entry;
}

}  // namespace

/// The script a person is asked to run.
///
/// Transmit writes it and never runs it, which is the right division and also
/// the reason it has to be right: the file is read, trusted and then run, on a
/// machine where the person has just typed their password. Every name and
/// every package identifier in it comes out of the archive's application list,
/// and an archive is the one thing in the room somebody else may have written.
///
/// Only one package manager exists on any given machine, so eight of the nine
/// scripts could not be produced here at all until the platform could be told
/// what to pretend to be.
class InstallScriptTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void picksWhatThisSystemCanInstall();
    void fallsBackToTheFormatsThatWorkAnywhere();
    void windowsHasNoFallbacksToOffer();
    void writesTheScriptThisSystemCanRun();
    void writesNothingWhenThereIsNothingToInstall();
    void aNameCannotBreakOutOfTheScript();
    void aNameCannotEscapeAComment();
    void aNameCannotBreakOutOfThePowerShellScript();
    void theScriptSaysItHasNotBeenRun();

private:
    [[nodiscard]] std::unique_ptr<testing::PlatformWithDrives> platformUsing(
        platform::PackageSource source, const QString& command,
        format::OsFamily os = format::OsFamily::Linux) const {
        auto fake = std::make_unique<testing::PlatformWithDrives>(
            platform::PlatformService::create(), QList<platform::StorageVolume>{});
        fake->pretendToBe(os);
        fake->usePackageManager(source, command);
        return fake;
    }

    [[nodiscard]] QString wrote(const InstallScriptWriter& writer, const InstallPlan& plan,
                                const QString& folder) const {
        const QString written = writer.write(plan, workspace_.filePath(folder));
        if (written.isEmpty()) {
            return {};
        }
        QFile file(written);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(file.readAll());
    }

    QTemporaryDir workspace_;
};

void InstallScriptTest::initTestCase() {
    QVERIFY(workspace_.isValid());
}

void InstallScriptTest::picksWhatThisSystemCanInstall() {
    const auto platform =
        platformUsing(platform::PackageSource::Apt, QStringLiteral("sudo apt install -y"));
    const InstallScriptWriter writer(*platform);

    const InstallPlan plan = writer.plan({
        app(QStringLiteral("Firefox"), {{QStringLiteral("apt"), QStringLiteral("firefox")}}),
        app(QStringLiteral("Krita"), {{QStringLiteral("dnf"), QStringLiteral("krita")}}),
        app(QStringLiteral("Something Bespoke"), {}),
    });

    // Named by what apt calls it, not by what some other distribution does.
    QCOMPARE(plan.installable.size(), 1);
    QCOMPARE(plan.installable.first().first, QStringLiteral("Firefox"));
    QCOMPARE(plan.installable.first().second, QStringLiteral("firefox"));

    // Krita is only in a manager this system does not have, and nothing
    // pretends otherwise.
    QCOMPARE(plan.manual,
             (QStringList{QStringLiteral("Krita"), QStringLiteral("Something Bespoke")}));
}

void InstallScriptTest::fallsBackToTheFormatsThatWorkAnywhere() {
    const auto platform =
        platformUsing(platform::PackageSource::Pacman, QStringLiteral("sudo pacman -S --needed"));
    const InstallScriptWriter writer(*platform);

    const InstallPlan plan = writer.plan({
        app(QStringLiteral("Bottles"),
            {{QStringLiteral("flatpak"), QStringLiteral("com.usebottles.bottles")}}),
        app(QStringLiteral("Chromium"), {{QStringLiteral("snap"), QStringLiteral("chromium")}}),
    });
    QCOMPARE(plan.installable.size(), 2);

    const QString script = wrote(writer, plan, QStringLiteral("fallbacks"));
    QVERIFY2(script.contains(QStringLiteral("flatpak install -y flathub 'com.usebottles.bottles'")),
             qPrintable(script));
    QVERIFY2(script.contains(QStringLiteral("sudo snap install 'chromium'")), qPrintable(script));

    // And each is guarded, because a machine that has neither should not stop
    // on a command it does not have.
    QVERIFY(script.contains(QStringLiteral("command -v flatpak")));
    QVERIFY(script.contains(QStringLiteral("command -v snap")));
}

void InstallScriptTest::windowsHasNoFallbacksToOffer() {
    // There is no flatpak on Windows, so an application winget does not have
    // is one to install by hand rather than one to pretend about.
    const auto platform =
        platformUsing(platform::PackageSource::Winget, QStringLiteral("winget install"),
                      format::OsFamily::Windows);
    const InstallScriptWriter writer(*platform);
    QCOMPARE(writer.scriptFileName(), QStringLiteral("install-apps.ps1"));

    const InstallPlan plan = writer.plan({
        app(QStringLiteral("Bottles"),
            {{QStringLiteral("flatpak"), QStringLiteral("com.usebottles.bottles")}}),
        app(QStringLiteral("Firefox"),
            {{QStringLiteral("winget"), QStringLiteral("Mozilla.Firefox")}}),
    });

    QCOMPARE(plan.installable.size(), 1);
    QCOMPARE(plan.manual, QStringList{QStringLiteral("Bottles")});
}

void InstallScriptTest::writesTheScriptThisSystemCanRun() {
    const auto platform =
        platformUsing(platform::PackageSource::Dnf, QStringLiteral("sudo dnf install -y"));
    const InstallScriptWriter writer(*platform);
    QCOMPARE(writer.scriptFileName(), QStringLiteral("install-apps.sh"));

    const InstallPlan plan = writer.plan({
        app(QStringLiteral("Krita"), {{QStringLiteral("dnf"), QStringLiteral("krita")}}),
        app(QStringLiteral("Inkscape"), {{QStringLiteral("dnf"), QStringLiteral("inkscape")}}),
    });

    const QString folder = QStringLiteral("shell");
    const QString script = wrote(writer, plan, folder);
    QVERIFY2(script.startsWith(QStringLiteral("#!/bin/sh\n")), qPrintable(script.left(40)));
    QVERIFY2(script.contains(QStringLiteral("sudo dnf install -y 'inkscape' 'krita'")),
             qPrintable(script));

    // One command with both packages, so the manager resolves them together
    // rather than being run twice.
    QCOMPARE(script.count(QStringLiteral("sudo dnf install -y")), 1);

#ifndef Q_OS_WIN
    // Runnable by the person it was written for, and by nobody else.
    const QFile::Permissions permissions =
        QFile(QDir(workspace_.filePath(folder)).filePath(QStringLiteral("install-apps.sh")))
            .permissions();
    QVERIFY(permissions.testFlag(QFile::ExeOwner));
    QVERIFY(!permissions.testFlag(QFile::WriteOther));
#endif
}

void InstallScriptTest::writesNothingWhenThereIsNothingToInstall() {
    const auto platform =
        platformUsing(platform::PackageSource::Apt, QStringLiteral("sudo apt install -y"));
    const InstallScriptWriter writer(*platform);

    const InstallPlan empty = writer.plan({});
    QVERIFY(empty.isEmpty());
    QVERIFY(writer.write(empty, workspace_.filePath(QStringLiteral("nothing"))).isEmpty());

    // And no folder made for a file that was never written.
    QVERIFY(!QDir(workspace_.filePath(QStringLiteral("nothing"))).exists());
}

void InstallScriptTest::aNameCannotBreakOutOfTheScript() {
    // Everything here came out of the archive. A package identifier carrying a
    // quote used to close the string it was pasted into, and what followed was
    // a line of a script somebody is about to run having just typed their
    // password.
    const QString hostile = QStringLiteral("bottles'; rm -rf ~; echo 'done");

    const auto platform =
        platformUsing(platform::PackageSource::Apt, QStringLiteral("sudo apt install -y"));
    const InstallScriptWriter writer(*platform);
    const InstallPlan plan = writer.plan({
        app(QStringLiteral("Innocent"), {{QStringLiteral("apt"), hostile}}),
        // The flatpak branch prints the names in a message when flatpak is
        // missing, which is where the hole was.
        app(QStringLiteral("Also Innocent"), {{QStringLiteral("flatpak"), hostile}}),
    });

    const QString script = wrote(writer, plan, QStringLiteral("hostile"));
    QVERIFY(!script.isEmpty());

    // The identifier is still carried - refusing it would be its own kind of
    // wrong - and every apostrophe in it appears as the four characters that
    // end a quoted string, add a literal quote and open one again. Checked as
    // the exact text rather than by counting quotes: an injection leaves an
    // even number of them too, which is why counting proves nothing.
    QString quoted = hostile;
    quoted.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
    QVERIFY2(script.contains(u'\'' + quoted + u'\''), qPrintable(script));

    // And nowhere does it appear as itself, which is what running off the end
    // of the string would look like.
    QVERIFY2(!script.contains(QStringLiteral("'") + hostile + QStringLiteral("'")),
             qPrintable(script));

    // The message printed when flatpak is missing is the one that used to
    // paste the names inside a quoted string rather than quoting the whole of
    // it. The names are in it, and it is one argument.
    QString message = QStringLiteral("Install flatpak first, or fetch these yourself: ") + hostile;
    message.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
    QVERIFY2(script.contains(QStringLiteral("echo '") + message + u'\''), qPrintable(script));
}

void InstallScriptTest::aNameCannotEscapeAComment() {
    // A comment ends at the newline. A name with one in it used to end the
    // comment, and the rest of the name became a line of the script.
    const QString hostile = QStringLiteral("Innocent\nrm -rf ~\n# ");

    const auto platform =
        platformUsing(platform::PackageSource::Apt, QStringLiteral("sudo apt install -y"));
    const InstallScriptWriter writer(*platform);
    const InstallPlan plan = writer.plan({app(hostile, {})});

    const QString script = wrote(writer, plan, QStringLiteral("comment"));
    QVERIFY(!script.isEmpty());

    for (const QString& line : script.split(u'\n')) {
        QVERIFY2(!line.trimmed().startsWith(QStringLiteral("rm -rf")),
                 qPrintable(QStringLiteral("a name became a command: ") + line));
    }
}

void InstallScriptTest::aNameCannotBreakOutOfThePowerShellScript() {
    const QString hostile = QStringLiteral("Innocent'; Remove-Item -Recurse ~; '");

    const auto platform =
        platformUsing(platform::PackageSource::Winget, QStringLiteral("winget install"),
                      format::OsFamily::Windows);
    const InstallScriptWriter writer(*platform);
    const InstallPlan plan = writer.plan({
        app(hostile, {{QStringLiteral("winget"), QStringLiteral("Some.Package")}}),
        app(hostile + QStringLiteral(" too"), {}),
    });

    const QString script = wrote(writer, plan, QStringLiteral("powershell"));
    QVERIFY(!script.isEmpty());

    // PowerShell escapes a quote by doubling it. Checked as the exact text the
    // name becomes, in both the places it is written: an injection also leaves
    // an even number of quotes on the line, so counting them proves nothing.
    QString doubled = hostile;
    doubled.replace(QStringLiteral("'"), QStringLiteral("''"));
    QVERIFY2(script.contains(QStringLiteral("Name = '") + doubled + u'\''), qPrintable(script));
    QVERIFY2(script.contains(QStringLiteral("Write-Host '  ") + doubled + QStringLiteral(" too'")),
             qPrintable(script));
    QVERIFY2(!script.contains(QStringLiteral("Write-Host '  ") + hostile), qPrintable(script));
}

void InstallScriptTest::theScriptSaysItHasNotBeenRun() {
    // The one promise the file itself has to make, because everything about
    // this design rests on the person reading it first.
    const auto platform =
        platformUsing(platform::PackageSource::Apt, QStringLiteral("sudo apt install -y"));
    const InstallScriptWriter writer(*platform);
    const InstallPlan plan = writer.plan(
        {app(QStringLiteral("Firefox"), {{QStringLiteral("apt"), QStringLiteral("firefox")}})});

    QVERIFY(wrote(writer, plan, QStringLiteral("promise"))
                .contains(QStringLiteral("Nothing here has been run")));

    const auto windows = platformUsing(platform::PackageSource::Winget,
                                       QStringLiteral("winget install"), format::OsFamily::Windows);
    const InstallScriptWriter windowsWriter(*windows);
    const InstallPlan windowsPlan =
        windowsWriter.plan({app(QStringLiteral("Firefox"),
                                {{QStringLiteral("winget"), QStringLiteral("Mozilla.Firefox")}})});
    QVERIFY(wrote(windowsWriter, windowsPlan, QStringLiteral("promise-windows"))
                .contains(QStringLiteral("Nothing here has been run")));
}

}  // namespace transmit::core

QTEST_GUILESS_MAIN(transmit::core::InstallScriptTest)
#include "InstallScriptTest.moc"
