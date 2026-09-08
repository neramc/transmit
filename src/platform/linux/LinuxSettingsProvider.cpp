#include "platform/linux/LinuxSettingsProvider.h"

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include "core/utils/Logging.h"

namespace transmit::platform {
namespace {

/// Runs a command and returns its trimmed output, or an empty string when the
/// tool is missing or fails. Every settings backend on Linux is reached this
/// way; none of them offers a stable library interface worth linking against.
QString run(const QString& program, const QStringList& arguments, int timeoutMs = 8000) {
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        return {};
    }

    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    if (process.exitCode() != 0) {
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

QString gsettingsGet(const QString& schema, const QString& key) {
    return settings_text::unquote(
        run(QStringLiteral("gsettings"), {QStringLiteral("get"), schema, key}));
}

bool gsettingsSet(const QString& schema, const QString& key, const QString& value) {
    return runSucceeds(QStringLiteral("gsettings"), {QStringLiteral("set"), schema, key, value});
}

/// KDE keeps its preferences in ini files under the config directory.
QString kdeRead(const QString& file, const QString& group, const QString& key) {
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u'/' + file;
    if (!QFile::exists(path)) {
        return {};
    }
    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(group);
    return settings.value(key).toString();
}

bool kdeWrite(const QString& file, const QString& group, const QString& key, const QString& value) {
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u'/' + file;
    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(group);
    settings.setValue(key, value);
    settings.endGroup();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

}  // namespace

namespace settings_text {

QString unquote(QString value) {
    value = value.trimmed();
    if (value.size() >= 2 && value.startsWith(u'\'') && value.endsWith(u'\'')) {
        return value.mid(1, value.size() - 2);
    }
    if (value.size() >= 2 && value.startsWith(u'"') && value.endsWith(u'"')) {
        return value.mid(1, value.size() - 2);
    }
    return value;
}

QString themeFromGnome(const QString& colorScheme, const QString& gtkTheme) {
    if (colorScheme == QLatin1String("prefer-dark")) {
        return QStringLiteral("dark");
    }
    if (colorScheme == QLatin1String("prefer-light")) {
        return QStringLiteral("light");
    }
    // Older desktops only express this through the theme name.
    if (gtkTheme.contains(QLatin1String("dark"), Qt::CaseInsensitive)) {
        return QStringLiteral("dark");
    }
    return gtkTheme.isEmpty() ? QString() : QStringLiteral("light");
}

QString layoutsFromGnomeSources(const QString& sources) {
    // Matched rather than deleted. This used to remove every bracket, quote
    // and the words "xkb" and "ibus" from the string and split what was left
    // on commas - which turned two layouts into three, the middle one empty,
    // and turned the empty list GNOME prints as "@a(ss) []" into a layout
    // called "@ass". Neither is a thing a person could have set.
    static const QRegularExpression pair(QStringLiteral(R"(\(\s*'([^']*)'\s*,\s*'([^']*)'\s*\))"));

    QStringList layouts;
    auto matches = pair.globalMatch(sources);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (match.captured(1) == QLatin1String("xkb")) {
            layouts << match.captured(2);
        }
    }
    return layouts.join(u',');
}

QString minutesFromGnomeSeconds(const QString& seconds) {
    // One rule for both, because which keys gsettings prints as "uint32 900"
    // and which as "900" is a property of each key's type in each version of
    // each schema, and getting that wrong reads as "this machine has no
    // setting for it" rather than as an error.
    QString text = seconds.trimmed();
    for (const QString& prefix : {QStringLiteral("uint32 "), QStringLiteral("int32 "),
                                  QStringLiteral("int64 "), QStringLiteral("uint64 ")}) {
        if (text.startsWith(prefix)) {
            text = text.mid(prefix.size()).trimmed();
            break;
        }
    }

    bool ok = false;
    const qlonglong value = text.toLongLong(&ok);
    if (!ok || value < 0) {
        return {};
    }
    return QString::number(value / 60);
}

QString normaliseLocale(QString locale) {
    // "ko_KR.UTF-8" is the same thing as BCP-47 "ko-KR".
    locale = locale.trimmed();
    const qsizetype dot = locale.indexOf(u'.');
    if (dot > 0) {
        // A modifier sits after the encoding - "sr_RS.UTF-8@latin" - and it is
        // part of which locale this is, so it survives the cut.
        const qsizetype at = locale.indexOf(u'@', dot);
        locale = at > 0 ? locale.left(dot) + locale.mid(at) : locale.left(dot);
    }
    return locale.replace(u'_', u'-');
}

QString toPosixLocale(const QString& bcp47) {
    QString locale = bcp47.trimmed();
    if (locale.isEmpty()) {
        return {};
    }

    QString modifier;
    const qsizetype at = locale.indexOf(u'@');
    if (at >= 0) {
        modifier = locale.mid(at);
        locale = locale.left(at);
    }

    locale.replace(u'-', u'_');
    return locale + QStringLiteral(".UTF-8") + modifier;
}

QString timezoneFromLink(const QString& linkTarget) {
    const qsizetype marker = linkTarget.indexOf(QStringLiteral("/zoneinfo/"));
    return marker >= 0 ? linkTarget.mid(marker + 10) : QString();
}

}  // namespace settings_text

LinuxSettingsProvider::LinuxSettingsProvider() : desktop_(detectDesktop()) {
    desktopName_ = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    if (desktopName_.isEmpty()) {
        desktopName_ = qEnvironmentVariable("DESKTOP_SESSION");
    }
}

LinuxSettingsProvider::Desktop LinuxSettingsProvider::detectDesktop() {
    const QString current = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    if (current.contains(QLatin1String("gnome")) || current.contains(QLatin1String("unity"))) {
        return Desktop::Gnome;
    }
    if (current.contains(QLatin1String("kde")) || current.contains(QLatin1String("plasma"))) {
        return Desktop::Kde;
    }
    if (current.contains(QLatin1String("xfce"))) {
        return Desktop::Xfce;
    }
    if (current.contains(QLatin1String("cinnamon"))) {
        return Desktop::Cinnamon;
    }
    if (current.contains(QLatin1String("mate"))) {
        return Desktop::Mate;
    }
    if (current.contains(QLatin1String("lxqt"))) {
        return Desktop::Lxqt;
    }
    return Desktop::Unknown;
}

bool LinuxSettingsProvider::usesGSettings() const {
    return desktop_ == Desktop::Gnome || desktop_ == Desktop::Cinnamon ||
           desktop_ == Desktop::Mate || desktop_ == Desktop::Unknown;
}

QString LinuxSettingsProvider::describeEnvironment() const {
    return desktopName_.isEmpty() ? QStringLiteral("Linux") : desktopName_;
}

QList<SettingValue> LinuxSettingsProvider::readAll() const {
    QList<SettingValue> values;
    const auto record = [&values](SettingKey key, const QString& value) {
        values.push_back(SettingValue{key, value, !value.isEmpty()});
    };

    // ------------------------------------------------------- appearance
    if (usesGSettings()) {
        record(SettingKey::AppearanceTheme,
               settings_text::themeFromGnome(
                   gsettingsGet(QStringLiteral("org.gnome.desktop.interface"),
                                QStringLiteral("color-scheme")),
                   gsettingsGet(QStringLiteral("org.gnome.desktop.interface"),
                                QStringLiteral("gtk-theme"))));
        record(SettingKey::AppearanceAccent,
               gsettingsGet(QStringLiteral("org.gnome.desktop.interface"),
                            QStringLiteral("accent-color")));

        QString wallpaper = gsettingsGet(QStringLiteral("org.gnome.desktop.background"),
                                         QStringLiteral("picture-uri"));
        if (wallpaper.startsWith(QLatin1String("file://"))) {
            wallpaper = QUrl(wallpaper).toLocalFile();
        }
        record(SettingKey::DesktopWallpaper, wallpaper);

        record(SettingKey::AccessibilityTextScale,
               gsettingsGet(QStringLiteral("org.gnome.desktop.interface"),
                            QStringLiteral("text-scaling-factor")));
        record(SettingKey::AccessibilityHighContrast,
               gsettingsGet(QStringLiteral("org.gnome.desktop.a11y.interface"),
                            QStringLiteral("high-contrast")));
        record(SettingKey::AccessibilityReduceMotion,
               gsettingsGet(QStringLiteral("org.gnome.desktop.interface"),
                            QStringLiteral("enable-animations")) == QLatin1String("false")
                   ? QStringLiteral("true")
                   : QStringLiteral("false"));
        record(SettingKey::MouseNaturalScroll,
               gsettingsGet(QStringLiteral("org.gnome.desktop.peripherals.touchpad"),
                            QStringLiteral("natural-scroll")));
        record(SettingKey::ClockUses24Hour,
               gsettingsGet(QStringLiteral("org.gnome.desktop.interface"),
                            QStringLiteral("clock-format")) == QLatin1String("24h")
                   ? QStringLiteral("true")
                   : QStringLiteral("false"));
        record(SettingKey::ShowHiddenFiles,
               gsettingsGet(QStringLiteral("org.gtk.Settings.FileChooser"),
                            QStringLiteral("show-hidden")));

        record(SettingKey::KeyboardLayouts,
               settings_text::layoutsFromGnomeSources(gsettingsGet(
                   QStringLiteral("org.gnome.desktop.input-sources"), QStringLiteral("sources"))));

        record(SettingKey::PowerSleepMinutes,
               settings_text::minutesFromGnomeSeconds(
                   gsettingsGet(QStringLiteral("org.gnome.settings-daemon.plugins.power"),
                                QStringLiteral("sleep-inactive-ac-timeout"))));
        record(SettingKey::PowerScreenOffMinutes,
               settings_text::minutesFromGnomeSeconds(gsettingsGet(
                   QStringLiteral("org.gnome.desktop.session"), QStringLiteral("idle-delay"))));
    } else if (desktop_ == Desktop::Kde) {
        const QString scheme = kdeRead(QStringLiteral("kdeglobals"), QStringLiteral("General"),
                                       QStringLiteral("ColorScheme"));
        record(SettingKey::AppearanceTheme,
               scheme.contains(QLatin1String("dark"), Qt::CaseInsensitive)
                   ? QStringLiteral("dark")
                   : (scheme.isEmpty() ? QString() : QStringLiteral("light")));
        record(SettingKey::AppearanceAccent,
               kdeRead(QStringLiteral("kdeglobals"), QStringLiteral("General"),
                       QStringLiteral("AccentColor")));
    }

    // ---------------------------------------------------------- locale
    QString language = qEnvironmentVariable("LANG");
    if (language.isEmpty()) {
        language = QLocale::system().name();
    }
    record(SettingKey::LocaleLanguage, settings_text::normaliseLocale(language));
    record(SettingKey::LocaleFormats,
           settings_text::normaliseLocale(qEnvironmentVariable("LC_TIME", language)));

    QString timezone = run(
        QStringLiteral("timedatectl"),
        {QStringLiteral("show"), QStringLiteral("--property=Timezone"), QStringLiteral("--value")});
    if (timezone.isEmpty()) {
        // Fall back to the symlink every systemd and non-systemd system keeps.
        timezone =
            settings_text::timezoneFromLink(QFile::symLinkTarget(QStringLiteral("/etc/localtime")));
    }
    record(SettingKey::LocaleTimezone, timezone);

    // --------------------------------------------------- default apps
    record(SettingKey::DefaultBrowser,
           run(QStringLiteral("xdg-settings"),
               {QStringLiteral("get"), QStringLiteral("default-web-browser")}));
    record(SettingKey::DefaultMailClient,
           run(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"),
                                            QStringLiteral("x-scheme-handler/mailto")}));
    // And the machine's own, from resources/system-map.json: the same
    // walk on every system, so it is done once rather than three times.
    values.append(readSystemSettings());

    return values;
}

ApplyResult LinuxSettingsProvider::apply(const SettingValue& value) const {
    if (!value.present || value.value.isEmpty()) {
        return {ApplyOutcome::Unsupported, QString(), QString()};
    }

    const auto applied = [] { return ApplyResult{ApplyOutcome::Applied, {}, {}}; };
    const auto failed = [](const QString& why) {
        return ApplyResult{ApplyOutcome::Failed, why, {}};
    };

    switch (value.key) {
        // The machine's own settings. Read from the table, and written into
        // the script rather than applied: none of these can be set without
        // rights this program does not ask for.
        case SettingKey::SystemHostname:
        case SettingKey::SystemHostsEntries:
        case SettingKey::SystemTimeServer:
        case SettingKey::SystemFirewallEnabled:
        case SettingKey::SystemRemoteLogin:
            return applySystemSetting(value);

        case SettingKey::AppearanceTheme: {
            if (desktop_ == Desktop::Kde) {
                const QString scheme = value.value == QLatin1String("dark")
                                           ? QStringLiteral("BreezeDark")
                                           : QStringLiteral("BreezeLight");
                return kdeWrite(QStringLiteral("kdeglobals"), QStringLiteral("General"),
                                QStringLiteral("ColorScheme"), scheme)
                           ? ApplyResult{ApplyOutcome::Approximated,
                                         QStringLiteral("set to %1").arg(scheme),
                                         {}}
                           : failed(QStringLiteral("could not write kdeglobals"));
            }
            const QString scheme =
                value.value == QLatin1String("dark")    ? QStringLiteral("prefer-dark")
                : value.value == QLatin1String("light") ? QStringLiteral("prefer-light")
                                                        : QStringLiteral("default");
            return gsettingsSet(QStringLiteral("org.gnome.desktop.interface"),
                                QStringLiteral("color-scheme"), scheme)
                       ? applied()
                       : failed(QStringLiteral("gsettings refused the value"));
        }

        case SettingKey::DesktopWallpaper: {
            if (!QFile::exists(value.value)) {
                return {
                    ApplyOutcome::Failed, QStringLiteral("the image is not on this computer"), {}};
            }
            const QString uri = QUrl::fromLocalFile(value.value).toString();
            const bool light = gsettingsSet(QStringLiteral("org.gnome.desktop.background"),
                                            QStringLiteral("picture-uri"), uri);
            // Newer GNOME keeps a separate image for the dark theme.
            gsettingsSet(QStringLiteral("org.gnome.desktop.background"),
                         QStringLiteral("picture-uri-dark"), uri);
            return light ? applied() : failed(QStringLiteral("gsettings refused the value"));
        }

        case SettingKey::LocaleTimezone:
            // Changing the clock is a system-wide act, so it is offered as a
            // command rather than performed with rights we should not hold.
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("setting the time zone affects every account"),
                    QStringLiteral("sudo timedatectl set-timezone %1").arg(value.value)};

        case SettingKey::LocaleLanguage:
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("the display language is set for the whole system"),
                    QStringLiteral("sudo localectl set-locale LANG=%1")
                        .arg(settings_text::toPosixLocale(value.value))};

        case SettingKey::KeyboardLayouts: {
            QStringList entries;
            for (const QString& layout : value.value.split(u',', Qt::SkipEmptyParts)) {
                entries << QStringLiteral("('xkb', '%1')").arg(layout.trimmed());
            }
            return gsettingsSet(QStringLiteral("org.gnome.desktop.input-sources"),
                                QStringLiteral("sources"),
                                u'[' + entries.join(QStringLiteral(", ")) + u']')
                       ? applied()
                       : failed(QStringLiteral("gsettings refused the layout list"));
        }

        case SettingKey::DefaultBrowser:
            return runSucceeds(
                       QStringLiteral("xdg-settings"),
                       {QStringLiteral("set"), QStringLiteral("default-web-browser"), value.value})
                       ? applied()
                       : ApplyResult{ApplyOutcome::Failed,
                                     QStringLiteral("that browser is not installed here"),
                                     {}};

        case SettingKey::AccessibilityTextScale:
            return gsettingsSet(QStringLiteral("org.gnome.desktop.interface"),
                                QStringLiteral("text-scaling-factor"), value.value)
                       ? applied()
                       : failed(QStringLiteral("gsettings refused the value"));

        case SettingKey::AccessibilityHighContrast:
            return gsettingsSet(QStringLiteral("org.gnome.desktop.a11y.interface"),
                                QStringLiteral("high-contrast"), value.value)
                       ? applied()
                       : failed(QStringLiteral("gsettings refused the value"));

        case SettingKey::AccessibilityReduceMotion:
            return gsettingsSet(QStringLiteral("org.gnome.desktop.interface"),
                                QStringLiteral("enable-animations"),
                                value.value == QLatin1String("true") ? QStringLiteral("false")
                                                                     : QStringLiteral("true"))
                       ? applied()
                       : failed(QStringLiteral("gsettings refused the value"));

        case SettingKey::MouseNaturalScroll:
            return gsettingsSet(QStringLiteral("org.gnome.desktop.peripherals.touchpad"),
                                QStringLiteral("natural-scroll"), value.value)
                       ? applied()
                       : failed(QStringLiteral("gsettings refused the value"));

        case SettingKey::ClockUses24Hour:
            return gsettingsSet(QStringLiteral("org.gnome.desktop.interface"),
                                QStringLiteral("clock-format"),
                                value.value == QLatin1String("true") ? QStringLiteral("24h")
                                                                     : QStringLiteral("12h"))
                       ? applied()
                       : failed(QStringLiteral("gsettings refused the value"));

        case SettingKey::ShowHiddenFiles:
            return gsettingsSet(QStringLiteral("org.gtk.Settings.FileChooser"),
                                QStringLiteral("show-hidden"), value.value)
                       ? applied()
                       : failed(QStringLiteral("gsettings refused the value"));

        case SettingKey::PowerScreenOffMinutes: {
            bool ok = false;
            const int minutes = value.value.toInt(&ok);
            if (!ok) {
                return failed(QStringLiteral("not a number of minutes"));
            }
            return gsettingsSet(QStringLiteral("org.gnome.desktop.session"),
                                QStringLiteral("idle-delay"),
                                QStringLiteral("uint32 %1").arg(minutes * 60))
                       ? applied()
                       : failed(QStringLiteral("gsettings refused the value"));
        }

        case SettingKey::PowerSleepMinutes: {
            bool ok = false;
            const int minutes = value.value.toInt(&ok);
            if (!ok) {
                return failed(QStringLiteral("not a number of minutes"));
            }
            return gsettingsSet(QStringLiteral("org.gnome.settings-daemon.plugins.power"),
                                QStringLiteral("sleep-inactive-ac-timeout"),
                                QString::number(minutes * 60))
                       ? applied()
                       : failed(QStringLiteral("gsettings refused the value"));
        }

        case SettingKey::AppearanceAccent:
            if (desktop_ == Desktop::Kde) {
                return kdeWrite(QStringLiteral("kdeglobals"), QStringLiteral("General"),
                                QStringLiteral("AccentColor"), value.value)
                           ? applied()
                           : failed(QStringLiteral("could not write kdeglobals"));
            }
            return gsettingsSet(QStringLiteral("org.gnome.desktop.interface"),
                                QStringLiteral("accent-color"), value.value)
                       ? applied()
                       : ApplyResult{ApplyOutcome::Unsupported,
                                     QStringLiteral("this desktop has no accent colour"),
                                     {}};

        case SettingKey::DefaultMailClient:
            return runSucceeds(QStringLiteral("xdg-mime"),
                               {QStringLiteral("default"), value.value,
                                QStringLiteral("x-scheme-handler/mailto")})
                       ? applied()
                       : ApplyResult{ApplyOutcome::Failed,
                                     QStringLiteral("that program is not installed here"),
                                     {}};

        case SettingKey::LocaleFormats:
            return {ApplyOutcome::NeedsPrivilege, QStringLiteral("format settings are system-wide"),
                    QStringLiteral("sudo localectl set-locale LC_TIME=%1")
                        .arg(settings_text::toPosixLocale(value.value))};
    }
    return {ApplyOutcome::Unsupported, {}, {}};
}

}  // namespace transmit::platform
