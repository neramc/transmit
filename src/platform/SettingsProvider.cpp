#include "platform/SettingsProvider.h"

#include <QCoreApplication>

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
    }
    return {};
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
            SettingKey::ShowHiddenFiles};
}

}  // namespace transmit::platform
