pragma Singleton

import QtQuick
import Transmit.ThemeState

/// Corner radii, from docs/design.md section 11.
///
/// Moderate on purpose. Heavily rounded corners read as a web page rather than
/// a desktop application, and section 11 rules out anything past 16 for
/// ordinary components. Pills are reserved for tags and status badges, where
/// the shape is carrying meaning rather than decoration.
QtObject {
    /// How round the corners are is the first thing that says which desktop a
    /// window belongs to, and every one of them has decided it differently:
    /// Xfce and Windows 10 are nearly square, Fluent and Adwaita are gently
    /// rounded, COSMIC is emphatically so. The numbers below are each
    /// desktop's own, and the last row is this design system's.
    readonly property var _byPlatform: ({
        "windows11": { chip: 4, control: 4, card: 8,  dialog: 8  },
        "windows10": { chip: 2, control: 2, card: 2,  dialog: 2  },
        "macos":     { chip: 4, control: 6, card: 10, dialog: 12 },
        "gnome":     { chip: 6, control: 8, card: 12, dialog: 12 },
        "kde":       { chip: 3, control: 4, card: 6,  dialog: 6  },
        "xfce":      { chip: 2, control: 2, card: 4,  dialog: 4  },
        "cosmic":    { chip: 8, control: 8, card: 16, dialog: 16 },
        "default":   { chip: 4, control: 6, card: 10, dialog: 14 }
    })

    readonly property var _current: _byPlatform[ThemeState.platform] || _byPlatform["default"]

    readonly property int chip:    _current.chip     ///< tags and small badges
    readonly property int control: _current.control  ///< buttons, inputs, combo boxes
    readonly property int card:    _current.card
    readonly property int dialog:  _current.dialog
    readonly property int pill:    999  ///< status badges only

    // Older names, mapped onto the scale above.
    readonly property int sm: chip
    readonly property int md: control
    readonly property int lg: card
}
