#include "platform/macos/MacOsSettingsProvider.h"

#include <QFile>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>

#include "core/utils/Logging.h"

#ifdef Q_OS_MACOS
#import <Foundation/Foundation.h>
#endif

namespace transmit::platform {
namespace {

QString run(const QString& program, const QStringList& arguments, int timeoutMs = 8000) {
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        return {};
    }
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs) || process.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

bool runSucceeds(const QString& program, const QStringList& arguments) {
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        return false;
    }
    QProcess process;
    process.start(program, arguments);
    return process.waitForFinished(8000) && process.exitCode() == 0;
}

QString defaultsRead(const QString& domain, const QString& key) {
    return run(QStringLiteral("defaults"), {QStringLiteral("read"), domain, key});
}

bool defaultsWrite(const QString& domain, const QString& key, const QString& type,
                   const QString& value) {
    return runSucceeds(QStringLiteral("defaults"),
                       {QStringLiteral("write"), domain, key, type, value});
}

}  // namespace

QString MacOsSettingsProvider::describeEnvironment() const { return QSysInfo::prettyProductName(); }

QList<SettingValue> MacOsSettingsProvider::readAll() const {
    QList<SettingValue> values;
    const auto record = [&values](SettingKey key, const QString& value) {
        values.push_back(SettingValue{key, value, !value.isEmpty()});
    };

    // macOS says nothing at all when the appearance is light, so the absence of
    // the key is the answer rather than a failure to read it.
    const QString style = defaultsRead(QStringLiteral("-g"), QStringLiteral("AppleInterfaceStyle"));
    record(SettingKey::AppearanceTheme,
           style.compare(QLatin1String("Dark"), Qt::CaseInsensitive) == 0
               ? QStringLiteral("dark")
               : QStringLiteral("light"));

    record(SettingKey::LocaleLanguage, QLocale::system().name().replace(u'_', u'-'));
    record(SettingKey::LocaleFormats,
           defaultsRead(QStringLiteral("-g"), QStringLiteral("AppleLocale")).replace(u'_', u'-'));
    record(SettingKey::LocaleTimezone,
           run(QStringLiteral("systemsetup"), {QStringLiteral("-gettimezone")})
               .section(u':', 1)
               .trimmed());
    record(SettingKey::ClockUses24Hour,
           defaultsRead(QStringLiteral("-g"), QStringLiteral("AppleICUForce24HourTime")) ==
                   QLatin1String("1")
               ? QStringLiteral("true")
               : QStringLiteral("false"));

    record(SettingKey::MouseNaturalScroll,
           defaultsRead(QStringLiteral("-g"), QStringLiteral("com.apple.swipescrolldirection")) ==
                   QLatin1String("1")
               ? QStringLiteral("true")
               : QStringLiteral("false"));
    record(SettingKey::AccessibilityReduceMotion,
           defaultsRead(QStringLiteral("com.apple.universalaccess"),
                        QStringLiteral("reduceMotion")) == QLatin1String("1")
               ? QStringLiteral("true")
               : QStringLiteral("false"));
    record(SettingKey::AccessibilityHighContrast,
           defaultsRead(QStringLiteral("com.apple.universalaccess"),
                        QStringLiteral("increaseContrast")) == QLatin1String("1")
               ? QStringLiteral("true")
               : QStringLiteral("false"));
    record(SettingKey::ShowHiddenFiles,
           defaultsRead(QStringLiteral("com.apple.finder"), QStringLiteral("AppleShowAllFiles")) ==
                   QLatin1String("1")
               ? QStringLiteral("true")
               : QStringLiteral("false"));

    // The desktop picture lives in a database rather than a preference, so it
    // is read through the scripting interface.
    record(SettingKey::DesktopWallpaper,
           run(QStringLiteral("osascript"),
               {QStringLiteral("-e"),
                QStringLiteral("tell application \"Finder\" to get POSIX path of "
                               "(get desktop picture as alias)")}));
    return values;
}

ApplyResult MacOsSettingsProvider::apply(const SettingValue& value) const {
    if (!value.present || value.value.isEmpty()) {
        return {ApplyOutcome::Unsupported, {}, {}};
    }

    const auto applied = [] { return ApplyResult{ApplyOutcome::Applied, {}, {}}; };
    const auto refused = [] {
        return ApplyResult{ApplyOutcome::Failed, QStringLiteral("macOS refused the value"), {}};
    };

    switch (value.key) {
        case SettingKey::AppearanceTheme:
            if (value.value == QLatin1String("dark")) {
                return defaultsWrite(QStringLiteral("-g"), QStringLiteral("AppleInterfaceStyle"),
                                     QStringLiteral("-string"), QStringLiteral("Dark"))
                           ? applied()
                           : refused();
            }
            // Light is the absence of the key, not a value of its own.
            return runSucceeds(QStringLiteral("defaults"),
                               {QStringLiteral("delete"), QStringLiteral("-g"),
                                QStringLiteral("AppleInterfaceStyle")})
                       ? applied()
                       : applied();  // already light, which is the desired end state

        case SettingKey::DesktopWallpaper:
            if (!QFile::exists(value.value)) {
                return {
                    ApplyOutcome::Failed, QStringLiteral("the image is not on this computer"), {}};
            }
            return runSucceeds(
                       QStringLiteral("osascript"),
                       {QStringLiteral("-e"),
                        QStringLiteral("tell application \"System Events\" to set picture of "
                                       "every desktop to \"%1\"")
                            .arg(value.value)})
                       ? applied()
                       : refused();

        case SettingKey::ClockUses24Hour:
            return defaultsWrite(QStringLiteral("-g"), QStringLiteral("AppleICUForce24HourTime"),
                                 QStringLiteral("-bool"), value.value)
                       ? applied()
                       : refused();

        case SettingKey::MouseNaturalScroll:
            return defaultsWrite(QStringLiteral("-g"),
                                 QStringLiteral("com.apple.swipescrolldirection"),
                                 QStringLiteral("-bool"), value.value)
                       ? applied()
                       : refused();

        case SettingKey::AccessibilityReduceMotion:
            return defaultsWrite(QStringLiteral("com.apple.universalaccess"),
                                 QStringLiteral("reduceMotion"), QStringLiteral("-bool"),
                                 value.value)
                       ? applied()
                       : refused();

        case SettingKey::AccessibilityHighContrast:
            return defaultsWrite(QStringLiteral("com.apple.universalaccess"),
                                 QStringLiteral("increaseContrast"), QStringLiteral("-bool"),
                                 value.value)
                       ? applied()
                       : refused();

        case SettingKey::ShowHiddenFiles:
            return defaultsWrite(QStringLiteral("com.apple.finder"),
                                 QStringLiteral("AppleShowAllFiles"), QStringLiteral("-bool"),
                                 value.value)
                       ? applied()
                       : refused();

        case SettingKey::LocaleTimezone:
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("the time zone applies to the whole computer"),
                    QStringLiteral("sudo systemsetup -settimezone %1").arg(value.value)};

        case SettingKey::LocaleLanguage:
        case SettingKey::LocaleFormats:
            return {
                ApplyOutcome::NeedsPrivilege,
                QStringLiteral("the display language is changed in System Settings"),
                QStringLiteral(
                    "open 'x-apple.systempreferences:com.apple.Localization-Settings.extension'")};

        case SettingKey::DefaultBrowser:
        case SettingKey::DefaultMailClient:
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("macOS asks you to confirm this change itself"),
                    QStringLiteral(
                        "open 'x-apple.systempreferences:com.apple.Desktop-Settings.extension'")};

        case SettingKey::AppearanceAccent:
        case SettingKey::KeyboardLayouts:
        case SettingKey::PowerSleepMinutes:
        case SettingKey::PowerScreenOffMinutes:
        case SettingKey::AccessibilityTextScale:
            break;
    }
    return {ApplyOutcome::Unsupported,
            QStringLiteral("macOS has no equivalent that can be set from here"),
            {}};
}

}  // namespace transmit::platform
