#include "SystemSettingsMap.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>

namespace transmit::platform {
namespace {

Q_LOGGING_CATEGORY(logSystemMap, "transmit.systemmap")

constexpr const char* kBuiltIn = ":/catalog/system-map.json";

/// How long a reader is given. These are small local queries - a file, a
/// registry value, `systemctl is-active` - and one that has not answered in
/// this long is one that is not going to.
constexpr int kReadTimeoutMs = 4000;

QString g_tablePath;
QString g_root;

/// Which section of the table applies here.
QString thisSystem() {
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

const QJsonArray& table() {
    static QJsonArray settings;
    static QString loadedFrom;

    const QString wanted = g_tablePath.isEmpty() ? QString::fromLatin1(kBuiltIn) : g_tablePath;
    if (loadedFrom == wanted) {
        return settings;
    }

    settings = {};
    loadedFrom = wanted;

    QFile file(wanted);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(logSystemMap) << "no system map at" << wanted;
        return settings;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qCWarning(logSystemMap) << "the system map is not valid JSON:" << error.errorString();
        return settings;
    }
    settings = document.object().value(QStringLiteral("settings")).toArray();
    return settings;
}

QJsonObject entryFor(SettingKey key) {
    const QString name = settingKeyName(key);
    for (const QJsonValue& value : table()) {
        const QJsonObject setting = value.toObject();
        if (setting.value(QStringLiteral("key")).toString() == name) {
            return setting.value(thisSystem()).toObject();
        }
    }
    return {};
}

/// A path the table names, moved under the test root when there is one.
QString locate(const QString& path) {
    if (g_root.isEmpty()) {
        return path;
    }
    QString relative = path;
    // Both shapes the table uses: "/etc/hosts" and "C:/Windows/...".
    if (relative.size() > 2 && relative.at(1) == QLatin1Char(':')) {
        relative = relative.mid(2);
    }
    while (relative.startsWith(QLatin1Char('/'))) {
        relative.remove(0, 1);
    }
    return QDir(g_root).filePath(relative);
}

QString runAndRead(const QJsonArray& commandLine) {
    if (commandLine.isEmpty()) {
        return {};
    }
    QStringList parts;
    for (const QJsonValue& piece : commandLine) {
        parts << piece.toString();
    }
    const QString program = parts.takeFirst();

    QProcess process;
    process.start(program, parts);
    if (!process.waitForFinished(kReadTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

/// The lines somebody added to a hosts file.
///
/// Every system ships the same handful - localhost, the IPv6 block, the
/// broadcast entries - and carrying those across would write a second copy of
/// them on the far side.
QString customHostsLines(const QString& contents) {
    static const QStringList shipped = {
        QStringLiteral("127.0.0.1"), QStringLiteral("::1"),
        QStringLiteral("127.0.1.1"), QStringLiteral("fe00::0"),
        QStringLiteral("ff00::0"),   QStringLiteral("ff02::1"),
        QStringLiteral("ff02::2"),   QStringLiteral("255.255.255.255"),
    };

    QStringList kept;
    for (const QString& raw : contents.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QString address = line.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0);
        if (shipped.contains(address)) {
            continue;
        }
        kept << line;
    }
    return kept.join(QLatin1Char('\n'));
}

/// The value of `Name=value` in an ini-shaped configuration file, ignoring a
/// commented-out default.
QString settingInFile(const QString& contents, const QString& name) {
    for (const QString& raw : contents.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('#')) || !line.startsWith(name + QLatin1Char('='))) {
            continue;
        }
        const QString value = line.mid(name.size() + 1).trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString readFileFor(const QJsonObject& how) {
    QFile file(locate(how.value(QStringLiteral("file")).toString()));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QString contents = QString::fromUtf8(file.readAll());

    if (how.value(QStringLiteral("lines")).toString() == QLatin1String("custom-hosts")) {
        return customHostsLines(contents);
    }
    const QString setting = how.value(QStringLiteral("setting")).toString();
    if (!setting.isEmpty()) {
        return settingInFile(contents, setting);
    }
    return contents.trimmed();
}

QString readRegistryFor(const QJsonObject& how) {
#if defined(Q_OS_WIN)
    const QString path = how.value(QStringLiteral("registry")).toString();
    const QString name = how.value(QStringLiteral("name")).toString();
    if (path.isEmpty() || name.isEmpty()) {
        return {};
    }
    QSettings registry(path, QSettings::NativeFormat);
    return registry.value(name).toString();
#else
    // Named in the table for the system it belongs to, and not reachable from
    // here. Saying so is the honest answer: an empty string would read as "the
    // setting is not set" rather than "this is not that system".
    Q_UNUSED(how);
    return {};
#endif
}

/// Turns what a reader said into "true" or "false", when the table asks for it.
QString asBooleanIfAsked(const QJsonObject& how, const QString& raw) {
    const QString whenEquals = how.value(QStringLiteral("booleanWhen")).toString();
    if (!whenEquals.isEmpty()) {
        return raw.compare(whenEquals, Qt::CaseInsensitive) == 0 ? QStringLiteral("true")
                                                                 : QStringLiteral("false");
    }
    const QString unlessEquals = how.value(QStringLiteral("booleanUnless")).toString();
    if (!unlessEquals.isEmpty()) {
        return raw.compare(unlessEquals, Qt::CaseInsensitive) == 0 ? QStringLiteral("false")
                                                                   : QStringLiteral("true");
    }
    return raw;
}

}  // namespace

QList<SettingKey> SystemSettingsMap::keys() {
    QList<SettingKey> found;
    for (const QJsonValue& value : table()) {
        const QString name = value.toObject().value(QStringLiteral("key")).toString();
        for (const SettingKey key : allSettingKeys()) {
            if (settingKeyName(key) == name) {
                found.append(key);
                break;
            }
        }
    }
    return found;
}

bool SystemSettingsMap::isSystemSetting(SettingKey key) {
    return keys().contains(key);
}

SettingValue SystemSettingsMap::read(SettingKey key) {
    const QJsonObject entry = entryFor(key);
    const QJsonObject how = entry.value(QStringLiteral("read")).toObject();
    if (how.isEmpty()) {
        return {key, {}, false};
    }

    QString raw;
    if (how.contains(QStringLiteral("file"))) {
        raw = readFileFor(how);
    } else if (how.contains(QStringLiteral("registry"))) {
        raw = readRegistryFor(how);
    } else if (how.contains(QStringLiteral("command"))) {
        // Not while the reads are pointed at a fixture: a test that checks how
        // a table is interpreted must not depend on what the machine running
        // it happens to have installed, or start processes on it.
        if (g_root.isEmpty()) {
            raw = runAndRead(how.value(QStringLiteral("command")).toArray());
        }
    }

    if (raw.isEmpty()) {
        return {key, {}, false};
    }
    return {key, asBooleanIfAsked(how, raw), true};
}

QString SystemSettingsMap::applyCommand(SettingKey key, const QString& value) {
    const QString command = entryFor(key).value(QStringLiteral("apply")).toString();
    if (command.isEmpty() || value.isEmpty()) {
        return {};
    }
    // Quoted where it is substituted, because a hostname somebody chose or a
    // line out of a hosts file is text this program did not write, and it is
    // going into a script somebody will run as root.
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QString(command).replace(QLatin1String("{value}"),
                                    QLatin1Char('\'') + quoted + QLatin1Char('\''));
}

void SystemSettingsMap::useTableForTesting(const QString& path) {
    g_tablePath = path;
}

void SystemSettingsMap::useRootForTesting(const QString& path) {
    g_root = path;
}

}  // namespace transmit::platform
