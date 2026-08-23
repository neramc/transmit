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

/// The accent colours macOS offers, in the order its own preference records
/// them, with the shade each one paints.
///
/// There is no arbitrary accent to set here: a machine arriving from a desktop
/// that allows one gets the nearest of these, and is told it was approximated.
struct AccentChoice {
    int index;
    int red;
    int green;
    int blue;
};

constexpr AccentChoice kAccents[] = {
    {-1, 0x8c, 0x8c, 0x8c},  // graphite
    {0, 0xff, 0x52, 0x57},   // red
    {1, 0xf7, 0x82, 0x1b},   // orange
    {2, 0xff, 0xc4, 0x09},   // yellow
    {3, 0x62, 0xba, 0x46},   // green
    {4, 0x00, 0x7a, 0xff},   // blue
    {5, 0xa5, 0x50, 0xa7},   // purple
    {6, 0xf7, 0x4f, 0x9e},   // pink
};

/// Splits "#rrggbb" into its channels. Written out rather than handed to
/// QColor because this layer links only QtCore.
bool parseHexColour(const QString& text, int& red, int& green, int& blue) {
    const QString digits = text.startsWith(u'#') ? text.mid(1) : text;
    if (digits.size() != 6) {
        return false;
    }
    bool ok = false;
    const uint packed = digits.toUInt(&ok, 16);
    if (!ok) {
        return false;
    }
    red = static_cast<int>((packed >> 16) & 0xFFu);
    green = static_cast<int>((packed >> 8) & 0xFFu);
    blue = static_cast<int>(packed & 0xFFu);
    return true;
}

/// The macOS accent closest to a colour, and whether it is close enough to
/// call it the same one. Squared distance in RGB is crude, but the palette is
/// eight well-separated colours and anything better would be pretending to a
/// precision the choice does not have.
const AccentChoice* nearestAccent(const QString& hex, bool& exact) {
    int red = 0;
    int green = 0;
    int blue = 0;
    if (!parseHexColour(hex, red, green, blue)) {
        return nullptr;
    }

    const AccentChoice* best = nullptr;
    long bestDistance = 0;
    for (const AccentChoice& choice : kAccents) {
        const long dr = choice.red - red;
        const long dg = choice.green - green;
        const long db = choice.blue - blue;
        const long distance = dr * dr + dg * dg + db * db;
        if (best == nullptr || distance < bestDistance) {
            best = &choice;
            bestDistance = distance;
        }
    }
    exact = bestDistance == 0;
    return best;
}

QString accentHex(int index) {
    for (const AccentChoice& choice : kAccents) {
        if (choice.index == index) {
            return QStringLiteral("#%1%2%3")
                .arg(choice.red, 2, 16, QLatin1Char('0'))
                .arg(choice.green, 2, 16, QLatin1Char('0'))
                .arg(choice.blue, 2, 16, QLatin1Char('0'));
        }
    }
    return {};
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

    // Absent when the user has never chosen one, which means the default blue.
    const QString accentIndex =
        defaultsRead(QStringLiteral("-g"), QStringLiteral("AppleAccentColor"));
    bool accentOk = false;
    const int accentValue = accentIndex.toInt(&accentOk);
    record(SettingKey::AppearanceAccent, accentOk ? accentHex(accentValue) : accentHex(4));

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

        case SettingKey::AppearanceAccent: {
            bool exact = false;
            const AccentChoice* choice = nearestAccent(value.value, exact);
            if (choice == nullptr) {
                return {ApplyOutcome::Failed, QStringLiteral("not a colour"), {}};
            }
            if (!defaultsWrite(QStringLiteral("-g"), QStringLiteral("AppleAccentColor"),
                               QStringLiteral("-int"), QString::number(choice->index))) {
                return refused();
            }
            if (exact) {
                return applied();
            }
            return {ApplyOutcome::Approximated,
                    QStringLiteral("macOS offers eight accent colours; this is the nearest"),
                    {}};
        }

        case SettingKey::PowerSleepMinutes:
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("sleep timings are set for the whole computer"),
                    QStringLiteral("sudo pmset -a sleep %1").arg(value.value)};

        case SettingKey::PowerScreenOffMinutes:
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("screen timings are set for the whole computer"),
                    QStringLiteral("sudo pmset -a displaysleep %1").arg(value.value)};

        case SettingKey::KeyboardLayouts:
            // The input source list is a plist of dictionaries keyed by
            // identifiers that differ from every other system's, and macOS
            // reads it once at login. Adding one by hand is quick; describing
            // it accurately in a script is not.
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("input sources are added from Settings"),
                    QStringLiteral(
                        "open 'x-apple.systempreferences:com.apple.Keyboard-Settings.extension'")};

        case SettingKey::AccessibilityTextScale:
            // Not a gap in this code: macOS scales the whole display rather
            // than text alone, and the per-application sizes it does offer are
            // each that application's own setting.
            return {ApplyOutcome::Unsupported,
                    QStringLiteral("macOS scales the display rather than text on its own"),
                    {}};
    }
    return {ApplyOutcome::Unsupported,
            QStringLiteral("macOS has no equivalent that can be set from here"),
            {}};
}

}  // namespace transmit::platform
