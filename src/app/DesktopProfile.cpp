#include "DesktopProfile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QOperatingSystemVersion>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QSysInfo>

namespace transmit::app {
namespace {

constexpr const char* kSettingsKey = "interface/desktopProfile";
constexpr const char* kSystem = "system";

/// Every look this program knows how to wear, in the order the setting lists
/// them. "default" is the design system's own, which is what a desktop nobody
/// here has measured gets.
const QStringList& profiles() {
    static const QStringList known = {
        QStringLiteral("windows11"), QStringLiteral("windows10"), QStringLiteral("macos"),
        QStringLiteral("gnome"),     QStringLiteral("kde"),       QStringLiteral("xfce"),
        QStringLiteral("cosmic"),    QStringLiteral("default"),
    };
    return known;
}

/// Which of them a desktop's own name means. XDG_CURRENT_DESKTOP is a
/// colon-separated list and is not consistent about case, which is why this
/// matches on a lowercased substring rather than comparing.
QString fromDesktopName(const QString& raw) {
    const QString name = raw.toLower();
    if (name.contains(QLatin1String("cosmic"))) {
        return QStringLiteral("cosmic");
    }
    if (name.contains(QLatin1String("kde")) || name.contains(QLatin1String("plasma"))) {
        return QStringLiteral("kde");
    }
    if (name.contains(QLatin1String("xfce"))) {
        return QStringLiteral("xfce");
    }
    // Last of the four, because "GNOME" appears in the list of several desktops
    // built on it - Unity and Cinnamon say so - and those are closer to GNOME
    // than to anything else here, so matching it last is also the right answer
    // for them.
    if (name.contains(QLatin1String("gnome")) || name.contains(QLatin1String("unity")) ||
        name.contains(QLatin1String("cinnamon"))) {
        return QStringLiteral("gnome");
    }
    return QStringLiteral("default");
}

}  // namespace

DesktopProfile::DesktopProfile(QObject* parent) : QObject(parent) {
    detected_ = detect();
    const QString stored =
        QSettings().value(QLatin1String(kSettingsKey), QString::fromLatin1(kSystem)).toString();
    current_ = profiles().contains(stored) ? stored : detected_;
    accent_ = readSystemAccent(detected_);
}

DesktopProfile& DesktopProfile::instance() {
    static DesktopProfile profile;
    return profile;
}

DesktopProfile* DesktopProfile::create(QQmlEngine* engine, QJSEngine* scriptEngine) {
    Q_UNUSED(engine);
    Q_UNUSED(scriptEngine);
    DesktopProfile& profile = instance();
    QJSEngine::setObjectOwnership(&profile, QJSEngine::CppOwnership);
    return &profile;
}

QStringList DesktopProfile::available() {
    QStringList all{QString::fromLatin1(kSystem)};
    all.append(profiles());
    return all;
}

QString DesktopProfile::nameOf(const QString& profile) {
    if (profile == QLatin1String(kSystem)) {
        return QCoreApplication::translate("DesktopProfile", "Match this desktop");
    }
    static const QMap<QString, const char*> names = {
        {QStringLiteral("windows11"), QT_TRANSLATE_NOOP("DesktopProfile", "Windows 11")},
        {QStringLiteral("windows10"), QT_TRANSLATE_NOOP("DesktopProfile", "Windows 10")},
        {QStringLiteral("macos"), QT_TRANSLATE_NOOP("DesktopProfile", "macOS")},
        {QStringLiteral("gnome"), QT_TRANSLATE_NOOP("DesktopProfile", "GNOME")},
        {QStringLiteral("kde"), QT_TRANSLATE_NOOP("DesktopProfile", "KDE Plasma")},
        {QStringLiteral("xfce"), QT_TRANSLATE_NOOP("DesktopProfile", "Xfce")},
        {QStringLiteral("cosmic"), QT_TRANSLATE_NOOP("DesktopProfile", "COSMIC")},
        {QStringLiteral("default"), QT_TRANSLATE_NOOP("DesktopProfile", "Transmit's own")},
    };
    if (const auto it = names.constFind(profile); it != names.constEnd()) {
        return QCoreApplication::translate("DesktopProfile", it.value());
    }
    return profile;
}

void DesktopProfile::use(const QString& profile) {
    const QString wanted = available().contains(profile) ? profile : QString::fromLatin1(kSystem);
    const QString resolved = wanted == QLatin1String(kSystem) ? detected_ : wanted;
    if (resolved == current_) {
        return;
    }
    current_ = resolved;
    QSettings().setValue(QLatin1String(kSettingsKey), wanted);
    emit changed();
}

QString DesktopProfile::detect() {
#if defined(Q_OS_WIN)
    // Windows 11 is 10.0.22000 and later. The product version string says "11"
    // on a current Qt, but the build number is what actually changed and is
    // what Microsoft's own documentation tells you to compare.
    const QOperatingSystemVersion version = QOperatingSystemVersion::current();
    return version.majorVersion() > 10 || version.microVersion() >= 22000
               ? QStringLiteral("windows11")
               : QStringLiteral("windows10");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    if (desktop.isEmpty()) {
        desktop = qEnvironmentVariable("XDG_SESSION_DESKTOP");
    }
    if (desktop.isEmpty()) {
        desktop = qEnvironmentVariable("DESKTOP_SESSION");
    }
    return fromDesktopName(desktop);
#endif
}

QColor DesktopProfile::readSystemAccent(const QString& profile) {
#if defined(Q_OS_WIN)
    Q_UNUSED(profile);
    // What the window manager tints its own chrome with, which is what a
    // person means by "my accent colour" on Windows. Stored as AABBGGRR.
    QSettings dwm(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM"),
                  QSettings::NativeFormat);
    bool ok = false;
    const quint32 packed = dwm.value(QStringLiteral("ColorizationColor")).toUInt(&ok);
    if (!ok || packed == 0) {
        return {};
    }
    return QColor::fromRgb((packed >> 16) & 0xFF, (packed >> 8) & 0xFF, packed & 0xFF);
#else
    if (profile != QLatin1String("kde")) {
        // GNOME keeps this behind gsettings and macOS behind defaults, and
        // both mean starting a process during start-up to read one colour.
        // Rather than pay that on every launch for two of the seven desktops,
        // they get the brand colour, which is a deliberate answer rather than
        // a wrong one.
        return {};
    }
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                         QStringLiteral("/kdeglobals");
    if (!QFile::exists(path)) {
        return {};
    }
    QSettings globals(path, QSettings::IniFormat);
    // Plasma 5.23 and later write AccentColor when one is set; before that,
    // and when it is left on "from wallpaper", the selection colour is what
    // the desktop is actually tinted with.
    for (const QString& key : {QStringLiteral("General/AccentColor"),
                               QStringLiteral("Colors:Selection/BackgroundNormal")}) {
        const QString value = globals.value(key).toString();
        const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (parts.size() >= 3) {
            return QColor::fromRgb(parts.at(0).toInt(), parts.at(1).toInt(), parts.at(2).toInt());
        }
    }
    return {};
#endif
}

}  // namespace transmit::app
