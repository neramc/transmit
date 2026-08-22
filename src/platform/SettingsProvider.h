#pragma once

#include <QList>
#include <QString>
#include <memory>

namespace transmit::platform {

/// The preferences Transmit carries between systems.
///
/// Each key is stated in a form that means the same thing everywhere, because
/// the whole point is to move between systems that express them differently.
/// Windows records its theme in the registry as a number, GNOME as a gsettings
/// enum, macOS as the presence or absence of a defaults string - and all three
/// are "light" or "dark" here.
enum class SettingKey {
    AppearanceTheme,      ///< "light", "dark" or "auto"
    AppearanceAccent,     ///< "#rrggbb"
    DesktopWallpaper,     ///< absolute path to the image
    LocaleLanguage,       ///< BCP-47, e.g. "ko-KR"
    LocaleFormats,        ///< BCP-47 used for dates, numbers and currency
    LocaleTimezone,       ///< IANA, e.g. "Asia/Seoul"
    KeyboardLayouts,      ///< comma-separated layout codes, most preferred first
    DefaultBrowser,       ///< the browser's own identifier on that system
    DefaultMailClient,
    PowerSleepMinutes,    ///< "0" means never
    PowerScreenOffMinutes,
    AccessibilityTextScale,   ///< a multiplier, e.g. "1.25"
    AccessibilityHighContrast,///< "true" or "false"
    AccessibilityReduceMotion,
    MouseNaturalScroll,
    ClockUses24Hour,
    ShowHiddenFiles,
};

QString settingKeyName(SettingKey key);
QString settingKeyDescription(SettingKey key);
QList<SettingKey> allSettingKeys();

struct SettingValue {
    SettingKey key = SettingKey::AppearanceTheme;
    QString value;

    /// False when this system had nothing to say about the setting, which is
    /// different from it being set to an empty value.
    bool present = false;
};

/// What happened when a setting was applied.
enum class ApplyOutcome {
    Applied,          ///< set exactly
    Approximated,     ///< the nearest thing this system offers
    NeedsPrivilege,   ///< possible, but not without elevation - a script is written instead
    Unsupported,      ///< this system has no equivalent
    Failed,
};

struct ApplyResult {
    ApplyOutcome outcome = ApplyOutcome::Unsupported;
    QString detail;

    /// A command the user can run themselves, for settings that need rights
    /// Transmit should not ask for.
    QString privilegedCommand;
};

/// Reads and writes the settings above on one operating system.
///
/// Every implementation is best-effort by design: a desktop Transmit has never
/// seen should degrade to carrying nothing rather than failing the restore.
class SettingsProvider {
public:
    virtual ~SettingsProvider() = default;

    /// Everything this system can tell us. Keys it has no answer for come back
    /// with `present` false rather than being omitted, so the report can say
    /// what was looked for.
    [[nodiscard]] virtual QList<SettingValue> readAll() const = 0;

    [[nodiscard]] virtual ApplyResult apply(const SettingValue& value) const = 0;

    /// Names the desktop or shell these settings came from, for the report.
    [[nodiscard]] virtual QString describeEnvironment() const = 0;
};

}  // namespace transmit::platform
