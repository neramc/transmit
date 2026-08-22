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

/// gsettings quotes strings and wraps lists; the values Transmit stores are
/// plain, so the decoration is stripped on the way in and added on the way out.
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

QString gsettingsGet(const QString& schema, const QString& key) {
    return unquote(run(QStringLiteral("gsettings"), {QStringLiteral("get"), schema, key}));
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

/// GNOME writes "'prefer-dark'" or "'default'"; Transmit stores light/dark/auto.
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

QString normaliseLocale(QString locale) {
    // "ko_KR.UTF-8" is the same thing as BCP-47 "ko-KR".
    const qsizetype dot = locale.indexOf(u'.');
    if (dot > 0) {
        locale = locale.left(dot);
    }
    return locale.replace(u'_', u'-');
}

QString toPosixLocale(const QString& bcp47) {
    QString locale = bcp47;
    locale.replace(u'-', u'_');
    return locale + QStringLiteral(".UTF-8");
}

}  // namespace

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
               themeFromGnome(gsettingsGet(QStringLiteral("org.gnome.desktop.interface"),
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

        // gsettings stores these as "['us', 'kr']" style tuples.
        QString sources = gsettingsGet(QStringLiteral("org.gnome.desktop.input-sources"),
                                       QStringLiteral("sources"));
        sources.remove(QRegularExpression(QStringLiteral(R"([\[\]\(\)'"]|xkb|ibus|,\s*(?=\)))")));
        record(SettingKey::KeyboardLayouts,
               sources.split(u',', Qt::SkipEmptyParts).join(u',').simplified().remove(u' '));

        record(SettingKey::PowerSleepMinutes, [] {
            const QString seconds =
                gsettingsGet(QStringLiteral("org.gnome.settings-daemon.plugins.power"),
                             QStringLiteral("sleep-inactive-ac-timeout"));
            bool ok = false;
            const int value = seconds.toInt(&ok);
            return ok ? QString::number(value / 60) : QString();
        }());
        record(SettingKey::PowerScreenOffMinutes, [] {
            // gsettings prints this one as "uint32 900".
            QString seconds = gsettingsGet(QStringLiteral("org.gnome.desktop.session"),
                                           QStringLiteral("idle-delay"));
            seconds.remove(QStringLiteral("uint32 "));
            bool ok = false;
            const int value = seconds.toInt(&ok);
            return ok ? QString::number(value / 60) : QString();
        }());
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
    record(SettingKey::LocaleLanguage, normaliseLocale(language));
    record(SettingKey::LocaleFormats, normaliseLocale(qEnvironmentVariable("LC_TIME", language)));

    QString timezone = run(
        QStringLiteral("timedatectl"),
        {QStringLiteral("show"), QStringLiteral("--property=Timezone"), QStringLiteral("--value")});
    if (timezone.isEmpty()) {
        // Fall back to the symlink every systemd and non-systemd system keeps.
        const QString link = QFile::symLinkTarget(QStringLiteral("/etc/localtime"));
        const qsizetype marker = link.indexOf(QStringLiteral("/zoneinfo/"));
        if (marker >= 0) {
            timezone = link.mid(marker + 10);
        }
    }
    record(SettingKey::LocaleTimezone, timezone);

    // --------------------------------------------------- default apps
    record(SettingKey::DefaultBrowser,
           run(QStringLiteral("xdg-settings"),
               {QStringLiteral("get"), QStringLiteral("default-web-browser")}));
    record(SettingKey::DefaultMailClient,
           run(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"),
                                            QStringLiteral("x-scheme-handler/mailto")}));
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
                        .arg(toPosixLocale(value.value))};

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
                        .arg(toPosixLocale(value.value))};
    }
    return {ApplyOutcome::Unsupported, {}, {}};
}

}  // namespace transmit::platform
