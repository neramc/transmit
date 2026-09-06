#include "platform/SettingsProvider.h"

#include <QCoreApplication>

#include "platform/SystemSettingsMap.h"

namespace transmit::platform {

QString settingKeyName(SettingKey key) {
    switch (key) {
        case SettingKey::AppearanceTheme:
            return QStringLiteral("appearance.theme");
        case SettingKey::AppearanceAccent:
            return QStringLiteral("appearance.accent");
        case SettingKey::DesktopWallpaper:
            return QStringLiteral("desktop.wallpaper");
        case SettingKey::LocaleLanguage:
            return QStringLiteral("locale.language");
        case SettingKey::LocaleFormats:
            return QStringLiteral("locale.formats");
        case SettingKey::LocaleTimezone:
            return QStringLiteral("locale.timezone");
        case SettingKey::KeyboardLayouts:
            return QStringLiteral("input.keyboard");
        case SettingKey::DefaultBrowser:
            return QStringLiteral("defaultApps.browser");
        case SettingKey::DefaultMailClient:
            return QStringLiteral("defaultApps.mail");
        case SettingKey::PowerSleepMinutes:
            return QStringLiteral("power.sleep");
        case SettingKey::PowerScreenOffMinutes:
            return QStringLiteral("power.screenOff");
        case SettingKey::AccessibilityTextScale:
            return QStringLiteral("a11y.textScale");
        case SettingKey::AccessibilityHighContrast:
            return QStringLiteral("a11y.highContrast");
        case SettingKey::AccessibilityReduceMotion:
            return QStringLiteral("a11y.reduceMotion");
        case SettingKey::MouseNaturalScroll:
            return QStringLiteral("input.naturalScroll");
        case SettingKey::ClockUses24Hour:
            return QStringLiteral("locale.clock24Hour");
        case SettingKey::ShowHiddenFiles:
            return QStringLiteral("files.showHidden");

        // The names the table in resources/system-map.json uses. They have to
        // match, and the catalogue test checks that they do.
        case SettingKey::SystemHostname:
            return QStringLiteral("SystemHostname");
        case SettingKey::SystemHostsEntries:
            return QStringLiteral("SystemHostsEntries");
        case SettingKey::SystemTimeServer:
            return QStringLiteral("SystemTimeServer");
        case SettingKey::SystemFirewallEnabled:
            return QStringLiteral("SystemFirewallEnabled");
        case SettingKey::SystemRemoteLogin:
            return QStringLiteral("SystemRemoteLogin");
    }
    return {};
}

QString settingKeyDescription(SettingKey key) {
    switch (key) {
        case SettingKey::AppearanceTheme:
            return QCoreApplication::translate("Settings", "Light or dark appearance");
        case SettingKey::AppearanceAccent:
            return QCoreApplication::translate("Settings", "Highlight colour");
        case SettingKey::DesktopWallpaper:
            return QCoreApplication::translate("Settings", "Desktop background");
        case SettingKey::LocaleLanguage:
            return QCoreApplication::translate("Settings", "Display language");
        case SettingKey::LocaleFormats:
            return QCoreApplication::translate("Settings", "Date, time and number formats");
        case SettingKey::LocaleTimezone:
            return QCoreApplication::translate("Settings", "Time zone");
        case SettingKey::KeyboardLayouts:
            return QCoreApplication::translate("Settings", "Keyboard layouts");
        case SettingKey::DefaultBrowser:
            return QCoreApplication::translate("Settings", "Default web browser");
        case SettingKey::DefaultMailClient:
            return QCoreApplication::translate("Settings", "Default mail program");
        case SettingKey::PowerSleepMinutes:
            return QCoreApplication::translate("Settings", "Sleep after");
        case SettingKey::PowerScreenOffMinutes:
            return QCoreApplication::translate("Settings", "Turn the screen off after");
        case SettingKey::AccessibilityTextScale:
            return QCoreApplication::translate("Settings", "Text size");
        case SettingKey::AccessibilityHighContrast:
            return QCoreApplication::translate("Settings", "High contrast");
        case SettingKey::AccessibilityReduceMotion:
            return QCoreApplication::translate("Settings", "Reduced motion");
        case SettingKey::MouseNaturalScroll:
            return QCoreApplication::translate("Settings", "Scrolling direction");
        case SettingKey::ClockUses24Hour:
            return QCoreApplication::translate("Settings", "24-hour clock");
        case SettingKey::ShowHiddenFiles:
            return QCoreApplication::translate("Settings", "Show hidden files");

        case SettingKey::SystemHostname:
            return QCoreApplication::translate("Settings", "The name this computer answers to");
        case SettingKey::SystemHostsEntries:
            return QCoreApplication::translate("Settings",
                                               "Names this computer resolves by itself");
        case SettingKey::SystemTimeServer:
            return QCoreApplication::translate("Settings",
                                               "The server this computer sets its clock from");
        case SettingKey::SystemFirewallEnabled:
            return QCoreApplication::translate("Settings", "Whether the firewall is on");
        case SettingKey::SystemRemoteLogin:
            return QCoreApplication::translate("Settings",
                                               "Whether this computer accepts SSH connections");
    }
    return {};
}

QList<SettingValue> SettingsProvider::readSystemSettings() {
    QList<SettingValue> values;
    for (const SettingKey key : SystemSettingsMap::keys()) {
        const SettingValue value = SystemSettingsMap::read(key);
        if (value.present) {
            values.append(value);
        }
    }
    return values;
}

ApplyResult SettingsProvider::applySystemSetting(const SettingValue& value) {
    const QString command = SystemSettingsMap::applyCommand(value.key, value.value);
    if (command.isEmpty()) {
        return {ApplyOutcome::Unsupported,
                QCoreApplication::translate("Settings",
                                            "this system has no way to set it from a script"),
                {}};
    }
    return {ApplyOutcome::NeedsPrivilege, {}, command};
}

QList<SettingKey> allSettingKeys() {
    return {SettingKey::AppearanceTheme,
            SettingKey::AppearanceAccent,
            SettingKey::DesktopWallpaper,
            SettingKey::LocaleLanguage,
            SettingKey::LocaleFormats,
            SettingKey::LocaleTimezone,
            SettingKey::KeyboardLayouts,
            SettingKey::DefaultBrowser,
            SettingKey::DefaultMailClient,
            SettingKey::PowerSleepMinutes,
            SettingKey::PowerScreenOffMinutes,
            SettingKey::AccessibilityTextScale,
            SettingKey::AccessibilityHighContrast,
            SettingKey::AccessibilityReduceMotion,
            SettingKey::MouseNaturalScroll,
            SettingKey::ClockUses24Hour,
            SettingKey::ShowHiddenFiles,
            SettingKey::SystemHostname,
            SettingKey::SystemHostsEntries,
            SettingKey::SystemTimeServer,
            SettingKey::SystemFirewallEnabled,
            SettingKey::SystemRemoteLogin};
}

}  // namespace transmit::platform
