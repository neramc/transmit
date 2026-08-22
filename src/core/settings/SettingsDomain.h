#pragma once

#include <QList>
#include <QString>

#include "core/continuity/ContinuityTypes.h"
#include "format/Bytes.h"
#include "platform/PlatformService.h"

namespace transmit::core {

using platform::ApplyOutcome;
using platform::SettingKey;
using platform::SettingValue;

/// A preference as it travelled, with where it came from.
struct CapturedSetting {
    SettingKey key = SettingKey::AppearanceTheme;
    QString value;
    QString sourceEnvironment;   ///< the desktop or shell it was read from
};

/// Captures and restores the desktop preferences that make a new machine feel
/// like the old one.
///
/// The values are normalised on the way out and translated back on the way in,
/// so a GNOME theme becomes a Windows one and a Windows keyboard layout becomes
/// a GNOME input source. Where a system will not let a program make the change,
/// nothing is forced: the command that would do it is written to a script the
/// user can read and run.
class SettingsDomain {
public:
    explicit SettingsDomain(const platform::PlatformService& platformService);

    /// Reads this machine's preferences.
    [[nodiscard]] QList<CapturedSetting> capture() const;

    /// Applies preferences from an archive, reporting what happened to each.
    /// `scriptDirectory` receives the privileged-command script, if any is
    /// needed; pass an empty string during a dry run.
    [[nodiscard]] QList<ContinuityNote> restore(const QList<CapturedSetting>& settings,
                                                const QString& scriptDirectory,
                                                bool dryRun) const;

    /// Wallpapers are files, and a wallpaper setting is worthless if the image
    /// stayed behind. This reports the image so the capture can include it.
    [[nodiscard]] static QString wallpaperPath(const QList<CapturedSetting>& settings);

    static format::ByteBuffer encode(const QList<CapturedSetting>& settings);
    static QList<CapturedSetting> decode(format::ByteView data);

private:
    const platform::PlatformService& platform_;
};

}  // namespace transmit::core
