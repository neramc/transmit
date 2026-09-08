#pragma once

#include <QString>

#include "platform/SettingsProvider.h"

namespace transmit::platform {

/// The rules that turn what a desktop says into what Transmit stores, and
/// back.
///
/// Declared here rather than left inside the reading, because every one of
/// them is a rule about text and none of them needs a machine running GNOME to
/// be asked about. Welded to the process call they could only be exercised on
/// a developer's own desktop, which is how "[('xkb', 'us'), ('xkb', 'kr')]"
/// came to be read as three layouts, one of them empty.
namespace settings_text {

/// Strips the quotes gsettings puts round a string.
[[nodiscard]] QString unquote(QString value);

/// light, dark, or nothing. GNOME says so outright from 42 onwards and only
/// through the theme's name before that.
[[nodiscard]] QString themeFromGnome(const QString& colorScheme, const QString& gtkTheme);

/// The keyboard layouts in a gsettings input-source list.
///
/// The value is a list of (type, id) pairs - "[('xkb', 'us'), ('ibus',
/// 'anthy')]" - and only the xkb ones are layouts. An ibus entry is an input
/// method, and applying it as a layout writes an input source that does not
/// exist onto the new machine, because writing them back is the one thing
/// this side has to agree with.
[[nodiscard]] QString layoutsFromGnomeSources(const QString& sources);

/// Whole minutes from a gsettings duration, which is printed as a bare number
/// or as "uint32 900" depending on the key's type. Nothing when it is neither.
[[nodiscard]] QString minutesFromGnomeSeconds(const QString& seconds);

/// "ko_KR.UTF-8" and "ko-KR" are the same language.
[[nodiscard]] QString normaliseLocale(QString locale);

/// And back, in the shape POSIX wants: the modifier goes after the encoding,
/// so "sr-RS@latin" is "sr_RS.UTF-8@latin" and not "sr_RS@latin.UTF-8", which
/// names no locale at all and leaves the machine on C.
[[nodiscard]] QString toPosixLocale(const QString& bcp47);

/// The zone in the target of /etc/localtime, or nothing if that is not what
/// the link points at.
[[nodiscard]] QString timezoneFromLink(const QString& linkTarget);

}  // namespace settings_text

/// Reads and writes desktop preferences on Linux.
///
/// There is no single place these live: GNOME and its derivatives use
/// gsettings, KDE uses its own configuration files, and several settings are
/// system-wide rather than per-user. The provider detects the desktop and uses
/// whichever backend applies, falling back to the parts that are common to all
/// of them.
class LinuxSettingsProvider final : public SettingsProvider {
public:
    LinuxSettingsProvider();

    [[nodiscard]] QList<SettingValue> readAll() const override;
    [[nodiscard]] ApplyResult apply(const SettingValue& value) const override;
    [[nodiscard]] QString describeEnvironment() const override;

private:
    enum class Desktop { Unknown, Gnome, Kde, Xfce, Cinnamon, Mate, Lxqt };

    [[nodiscard]] static Desktop detectDesktop();
    [[nodiscard]] bool usesGSettings() const;

    Desktop desktop_ = Desktop::Unknown;
    QString desktopName_;
};

}  // namespace transmit::platform
