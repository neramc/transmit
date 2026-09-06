// The table that says where each system keeps its own settings.
//
// Two of the three systems it describes cannot be run by whoever is changing
// it. That is the reason the knowledge is a table rather than three
// implementations, and it is also the reason this file exists: what can be
// checked from here is the table's shape, the rules it has to follow, and the
// one thing that must never happen - a machine setting being applied rather
// than written down for somebody to read first.

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "platform/SettingsProvider.h"
#include "platform/SystemSettingsMap.h"

namespace transmit::platform {
namespace {

const QString& tablePath() {
    static const QString path = QStringLiteral(TRANSMIT_SOURCE_DIR "/resources/system-map.json");
    return path;
}

QJsonObject theTable() {
    QFile file(tablePath());
    EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << "no table at " << tablePath().toStdString();
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    EXPECT_EQ(error.error, QJsonParseError::NoError) << error.errorString().toStdString();
    return document.object();
}

TEST(SystemMap, DescribesEverySettingOnEverySystem) {
    const QJsonArray settings = theTable().value(QStringLiteral("settings")).toArray();
    ASSERT_FALSE(settings.isEmpty());

    for (const QJsonValue& value : settings) {
        const QJsonObject setting = value.toObject();
        const QString key = setting.value(QStringLiteral("key")).toString();
        EXPECT_FALSE(key.isEmpty());
        EXPECT_FALSE(setting.value(QStringLiteral("description")).toString().isEmpty())
            << key.toStdString()
            << " has no description, and the description is what a person "
               "reads in the report";

        // Every system, so that a setting is never quietly missing on the one
        // nobody here runs.
        for (const char* system : {"linux", "windows", "macos"}) {
            const QJsonObject entry = setting.value(QLatin1String(system)).toObject();
            EXPECT_FALSE(entry.isEmpty()) << key.toStdString() << " says nothing about " << system;

            const QJsonObject how = entry.value(QStringLiteral("read")).toObject();
            EXPECT_FALSE(how.isEmpty())
                << key.toStdString() << " has no way to be read on " << system;
            const bool reachable = how.contains(QStringLiteral("file")) ||
                                   how.contains(QStringLiteral("registry")) ||
                                   how.contains(QStringLiteral("command"));
            EXPECT_TRUE(reachable) << key.toStdString() << " on " << system
                                   << " names no file, registry value or command";

            EXPECT_FALSE(entry.value(QStringLiteral("apply")).toString().isEmpty())
                << key.toStdString() << " has no way to be set on " << system;
        }
    }
}

// Every name in the table has to be a key the code knows, and every key the
// code calls a system setting has to be in the table. A mismatch is a setting
// that is read and then never applied, or applied from a table entry nothing
// reads.
TEST(SystemMap, ItsNamesAndTheCodesAgree) {
    const QJsonArray settings = theTable().value(QStringLiteral("settings")).toArray();

    QStringList inTable;
    for (const QJsonValue& value : settings) {
        inTable << value.toObject().value(QStringLiteral("key")).toString();
    }

    QStringList known;
    for (const SettingKey key : allSettingKeys()) {
        known << settingKeyName(key);
    }

    for (const QString& name : inTable) {
        EXPECT_TRUE(known.contains(name))
            << name.toStdString() << " is in the table and is not a setting the code has";
    }

    SystemSettingsMap::useTableForTesting(tablePath());
    for (const SettingKey key : SystemSettingsMap::keys()) {
        EXPECT_TRUE(inTable.contains(settingKeyName(key)));
    }
    EXPECT_EQ(SystemSettingsMap::keys().size(), inTable.size());
    SystemSettingsMap::useTableForTesting({});
}

// A command that puts a value into a shell has to survive a value that
// contains a quote, a semicolon or a newline - and a hosts file is text a
// person typed, which is exactly where such a thing comes from.
TEST(SystemMap, AValueCannotBreakOutOfTheCommandItGoesInto) {
    SystemSettingsMap::useTableForTesting(tablePath());

    for (const QString& hostile :
         {QStringLiteral("name'; rm -rf /; echo '"), QStringLiteral("name$(whoami)"),
          QStringLiteral("name`id`"), QStringLiteral("name\nrm -rf /"),
          QStringLiteral("name && reboot")}) {
        const QString command =
            SystemSettingsMap::applyCommand(SettingKey::SystemHostname, hostile);
        ASSERT_FALSE(command.isEmpty());

        // Everything the value contributed is inside one pair of single
        // quotes, which is the only place a shell reads nothing.
        const QString afterQuote = command.section(QLatin1Char('\''), 1);
        EXPECT_FALSE(afterQuote.isEmpty());
        EXPECT_TRUE(command.contains(QStringLiteral("'\\''")) ||
                    !hostile.contains(QLatin1Char('\'')))
            << "a quote in the value was not escaped: " << command.toStdString();
    }

    SystemSettingsMap::useTableForTesting({});
}

// The whole point of the design: reading is done here, and setting is not.
TEST(SystemMap, SettingOneIsAlwaysHandedBackRatherThanDone) {
    SystemSettingsMap::useTableForTesting(tablePath());

    for (const SettingKey key : SystemSettingsMap::keys()) {
        const SettingValue value{key, QStringLiteral("something"), true};
        const ApplyResult result = SettingsProvider::applySystemSetting(value);
        EXPECT_EQ(result.outcome, ApplyOutcome::NeedsPrivilege)
            << settingKeyName(key).toStdString() << " was not left for the user to run";
        EXPECT_FALSE(result.privilegedCommand.isEmpty());
    }

    SystemSettingsMap::useTableForTesting({});
}

// The readers are checked against a fixture rather than against whatever the
// machine running the test happens to have in /etc.
TEST(SystemMap, ReadsWhatTheTableSaysToRead) {
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    ASSERT_TRUE(QDir(root.path()).mkpath(QStringLiteral("etc/systemd")));

    const auto write = [&root](const QString& relative, const QString& contents) {
        QFile file(QDir(root.path()).filePath(relative));
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(contents.toUtf8());
    };

    write(QStringLiteral("etc/hostname"), QStringLiteral("workshop\n"));
    write(QStringLiteral("etc/hosts"), QStringLiteral("# a comment\n"
                                                      "127.0.0.1\tlocalhost\n"
                                                      "::1\t\tip6-localhost\n"
                                                      "192.168.1.20\tprinter\n"
                                                      "10.0.0.5\tbuild-server build\n"));
    write(QStringLiteral("etc/systemd/timesyncd.conf"),
          QStringLiteral("[Time]\n#NTP=\nNTP=time.example.org\n"));

    SystemSettingsMap::useTableForTesting(tablePath());
    SystemSettingsMap::useRootForTesting(root.path());

    const SettingValue hostname = SystemSettingsMap::read(SettingKey::SystemHostname);
    EXPECT_TRUE(hostname.present);
    EXPECT_EQ(hostname.value, QStringLiteral("workshop"));

    // Only the lines somebody added. Carrying the shipped ones across would
    // write a second copy of them on the far side.
    const SettingValue hosts = SystemSettingsMap::read(SettingKey::SystemHostsEntries);
    EXPECT_TRUE(hosts.present);
    EXPECT_EQ(hosts.value, QStringLiteral("192.168.1.20\tprinter\n10.0.0.5\tbuild-server build"));

    // The commented-out default is not the setting.
    const SettingValue ntp = SystemSettingsMap::read(SettingKey::SystemTimeServer);
    EXPECT_TRUE(ntp.present);
    EXPECT_EQ(ntp.value, QStringLiteral("time.example.org"));

    SystemSettingsMap::useRootForTesting({});
    SystemSettingsMap::useTableForTesting({});
}

}  // namespace
}  // namespace transmit::platform
