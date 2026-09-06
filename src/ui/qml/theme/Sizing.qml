pragma Singleton

import QtQuick
import Transmit.ThemeState

/// Control metrics.
///
/// Desktop controls want a consistent height above all else - a row of a
/// button, a field and a combo box should line up without anyone nudging
/// margins to make it happen.
QtObject {
    // Section 14: 32 to 40, and nothing taller. A form control the height of a
    // phone's is the clearest single sign of a mobile interface stretched to
    // fit a desktop.
    /// How tall a control is and how much room a row gets is the other half of
    /// looking like you belong: GNOME and COSMIC are generous, macOS is
    /// compact, Windows sits between them. These are each desktop's own.
    readonly property var _byPlatform: ({
        "windows11": { small: 28, control: 32, large: 40, row: 36 },
        "windows10": { small: 26, control: 30, large: 38, row: 34 },
        "macos":     { small: 24, control: 28, large: 36, row: 32 },
        "gnome":     { small: 30, control: 36, large: 44, row: 40 },
        "kde":       { small: 26, control: 30, large: 38, row: 34 },
        "xfce":      { small: 24, control: 28, large: 36, row: 32 },
        "cosmic":    { small: 30, control: 36, large: 44, row: 40 },
        "default":   { small: 28, control: 32, large: 40, row: 36 }
    })

    readonly property var _current: _byPlatform[ThemeState.platform] || _byPlatform["default"]

    readonly property int controlHeightSmall: _current.small
    readonly property int controlHeight:      _current.control
    readonly property int controlHeightLarge: _current.large

    /// The smallest a thing can be and still be reliably hit with a pointer.
    /// Nothing interactive may be smaller in either direction.
    readonly property int minimumTouchTarget: 24

    // Section 24. Rows are compact because desktop applications are good at
    // dense data and bad at making people scroll for it.
    readonly property int rowHeightCompact:     32
    readonly property int rowHeight:            _current.row
    readonly property int rowHeightComfortable: 44

    // Section 26. One family, five sizes, no drifting in between.
    readonly property int iconSizeSmall:  16   ///< inside compact controls
    readonly property int iconSize:       18   ///< secondary controls
    readonly property int iconSizeMedium: 20   ///< standard
    readonly property int iconSizeLarge:  24   ///< navigation and prominent controls
    readonly property int iconSizeHero:   32   ///< empty states, result screens

    // Section 4. The sidebar collapses rather than permanently taking a fifth
    // of a 1280-wide window.
    readonly property int sidebarWidth:        240
    readonly property int sidebarWidthCompact: 64

    readonly property int toolbarHeight:   48
    readonly property int statusBarHeight: 28

    // Section 6. Content stops growing past these; a line much longer than
    // about 90 characters is measurably harder to read, and a form stretched
    // across a 4K display is worse than one that stays where the eye is.
    readonly property int maxContentWidth:     1200  ///< standard content
    readonly property int maxTextWidth:        800   ///< text-heavy content
    // Wide data - tables, trees - has no maximum and fills the window.

    // Section 32. Below `mediumWindow` the interface tightens up; below
    // `smallWindow` the sidebar collapses on its own.
    readonly property int mediumWindow: 1100
    readonly property int smallWindow:  860
}
