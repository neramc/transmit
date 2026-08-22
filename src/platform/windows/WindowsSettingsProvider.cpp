#include "platform/windows/WindowsSettingsProvider.h"

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QSettings>
#include <QSysInfo>

#include "core/utils/Logging.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace transmit::platform {
namespace {

constexpr const char* kPersonalize =
    "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr const char* kInternational = "HKEY_CURRENT_USER\\Control Panel\\Desktop";
constexpr const char* kControlInternational = "HKEY_CURRENT_USER\\Control Panel\\International";
constexpr const char* kAccessibility = "HKEY_CURRENT_USER\\Control Panel\\Accessibility";
constexpr const char* kExplorerAdvanced =
    "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
constexpr const char* kDwm = "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\DWM";

QString registryString(const char* path, const QString& key) {
    QSettings settings(QString::fromLatin1(path), QSettings::NativeFormat);
    const QVariant value = settings.value(key);
    return value.isValid() ? value.toString() : QString();
}

bool registrySetString(const char* path, const QString& key, const QString& value) {
    QSettings settings(QString::fromLatin1(path), QSettings::NativeFormat);
    settings.setValue(key, value);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool registrySetDword(const char* path, const QString& key, quint32 value) {
    QSettings settings(QString::fromLatin1(path), QSettings::NativeFormat);
    settings.setValue(key, value);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

/// DWM stores the highlight colour as 0xAABBGGRR, which is neither the order
/// nor the channel arrangement anyone else uses.
QString accentFromDwm(const QString& raw) {
    bool ok = false;
    const quint32 value = raw.toUInt(&ok);
    if (!ok) {
        return {};
    }
    const quint32 red = value & 0xFFu;
    const quint32 green = (value >> 8) & 0xFFu;
    const quint32 blue = (value >> 16) & 0xFFu;
    return QStringLiteral("#%1%2%3")
        .arg(red, 2, 16, QLatin1Char('0'))
        .arg(green, 2, 16, QLatin1Char('0'))
        .arg(blue, 2, 16, QLatin1Char('0'));
}

}  // namespace

QString WindowsSettingsProvider::describeEnvironment() const {
    return QSysInfo::prettyProductName();
}

QList<SettingValue> WindowsSettingsProvider::readAll() const {
    QList<SettingValue> values;
    const auto record = [&values](SettingKey key, const QString& value) {
        values.push_back(SettingValue{key, value, !value.isEmpty()});
    };

    const QString appsLight = registryString(kPersonalize, QStringLiteral("AppsUseLightTheme"));
    record(SettingKey::AppearanceTheme,
           appsLight.isEmpty() ? QString()
                               : (appsLight == QLatin1String("0") ? QStringLiteral("dark")
                                                                  : QStringLiteral("light")));
    record(SettingKey::AppearanceAccent,
           accentFromDwm(registryString(kDwm, QStringLiteral("ColorizationColor"))));
    record(SettingKey::DesktopWallpaper,
           registryString(kInternational, QStringLiteral("WallPaper")));

    record(SettingKey::LocaleLanguage, QLocale::system().name().replace(u'_', u'-'));
    record(SettingKey::LocaleFormats,
           registryString(kControlInternational, QStringLiteral("LocaleName")));
    record(SettingKey::ClockUses24Hour,
           registryString(kControlInternational, QStringLiteral("iTime")) == QLatin1String("1")
               ? QStringLiteral("true")
               : QStringLiteral("false"));

#ifdef Q_OS_WIN
    DYNAMIC_TIME_ZONE_INFORMATION zone{};
    if (GetDynamicTimeZoneInformation(&zone) != TIME_ZONE_ID_INVALID) {
        // The Windows name, not an IANA one; the restore side maps it or asks.
        record(SettingKey::LocaleTimezone, QString::fromWCharArray(zone.TimeZoneKeyName));
    }
#endif

    record(SettingKey::KeyboardLayouts, [] {
        QSettings preload(QStringLiteral("HKEY_CURRENT_USER\\Keyboard Layout\\Preload"),
                          QSettings::NativeFormat);
        QStringList layouts;
        for (const QString& key : preload.childKeys()) {
            layouts << preload.value(key).toString();
        }
        return layouts.join(u',');
    }());

    record(SettingKey::AccessibilityHighContrast, [] {
        const QString flags = registryString(kAccessibility, QStringLiteral("HighContrast"));
        Q_UNUSED(flags);
        QSettings settings(
            QStringLiteral("HKEY_CURRENT_USER\\Control Panel\\Accessibility\\HighContrast"),
            QSettings::NativeFormat);
        const quint32 value = settings.value(QStringLiteral("Flags")).toUInt();
        return (value & 0x01u) != 0 ? QStringLiteral("true") : QStringLiteral("false");
    }());

    record(SettingKey::ShowHiddenFiles,
           registryString(kExplorerAdvanced, QStringLiteral("Hidden")) == QLatin1String("1")
               ? QStringLiteral("true")
               : QStringLiteral("false"));

    const QString browser = registryString(
        "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\"
        "http\\UserChoice",
        QStringLiteral("ProgId"));
    record(SettingKey::DefaultBrowser, browser);
    return values;
}

ApplyResult WindowsSettingsProvider::apply(const SettingValue& value) const {
    if (!value.present || value.value.isEmpty()) {
        return {ApplyOutcome::Unsupported, {}, {}};
    }

    switch (value.key) {
        case SettingKey::AppearanceTheme: {
            const quint32 light = value.value == QLatin1String("dark") ? 0u : 1u;
            const bool apps =
                registrySetDword(kPersonalize, QStringLiteral("AppsUseLightTheme"), light);
            registrySetDword(kPersonalize, QStringLiteral("SystemUsesLightTheme"), light);
            return apps ? ApplyResult{ApplyOutcome::Applied, {}, {}}
                        : ApplyResult{ApplyOutcome::Failed,
                                      QStringLiteral("the registry refused the value"),
                                      {}};
        }

        case SettingKey::DesktopWallpaper: {
            if (!QFile::exists(value.value)) {
                return {
                    ApplyOutcome::Failed, QStringLiteral("the image is not on this computer"), {}};
            }
#ifdef Q_OS_WIN
            const std::wstring native = QDir::toNativeSeparators(value.value).toStdWString();
            if (SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t*>(native.c_str()),
                                      SPIF_UPDATEINIFILE | SPIF_SENDCHANGE) != 0) {
                return {ApplyOutcome::Applied, {}, {}};
            }
#endif
            return {ApplyOutcome::Failed, QStringLiteral("Windows refused the image"), {}};
        }

        case SettingKey::ShowHiddenFiles:
            return registrySetDword(kExplorerAdvanced, QStringLiteral("Hidden"),
                                    value.value == QLatin1String("true") ? 1u : 2u)
                       ? ApplyResult{ApplyOutcome::Applied, {}, {}}
                       : ApplyResult{ApplyOutcome::Failed, {}, {}};

        case SettingKey::ClockUses24Hour:
            return registrySetString(kControlInternational, QStringLiteral("iTime"),
                                     value.value == QLatin1String("true") ? QStringLiteral("1")
                                                                          : QStringLiteral("0"))
                       ? ApplyResult{ApplyOutcome::Applied, {}, {}}
                       : ApplyResult{ApplyOutcome::Failed, {}, {}};

        case SettingKey::DefaultBrowser:
            // Windows signs this choice with a hash it computes from the user's
            // identity, and rejects a value written any other way. There is no
            // honest way to set it programmatically, so the user is sent to the
            // page that does it.
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("Windows only accepts this change from its Settings app"),
                    QStringLiteral("start ms-settings:defaultapps")};

        case SettingKey::LocaleTimezone:
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("the time zone applies to the whole computer"),
                    QStringLiteral("tzutil /s \"%1\"").arg(value.value)};

        case SettingKey::LocaleLanguage:
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("the display language needs the language pack installed"),
                    QStringLiteral("start ms-settings:regionlanguage")};

        case SettingKey::KeyboardLayouts:
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("keyboard layouts are added through Settings"),
                    QStringLiteral("start ms-settings:regionlanguage")};

        case SettingKey::AppearanceAccent:
        case SettingKey::AccessibilityHighContrast:
        case SettingKey::AccessibilityTextScale:
        case SettingKey::AccessibilityReduceMotion:
        case SettingKey::MouseNaturalScroll:
        case SettingKey::PowerSleepMinutes:
        case SettingKey::PowerScreenOffMinutes:
        case SettingKey::DefaultMailClient:
        case SettingKey::LocaleFormats:
            break;
    }

    // Everything else is either machine-wide or has no registry equivalent
    // that can be set without Windows noticing and reverting it.
    return {ApplyOutcome::Unsupported,
            QStringLiteral("Windows does not let a program change this on your behalf"),
            {}};
}

}  // namespace transmit::platform
