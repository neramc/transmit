pragma Singleton

import QtQuick

/// Control metrics.
///
/// Desktop controls want a consistent height above all else - a row of a
/// button, a field and a combo box should line up without anyone nudging
/// margins to make it happen.
QtObject {
    // Section 14: 32 to 40, and nothing taller. A form control the height of a
    // phone's is the clearest single sign of a mobile interface stretched to
    // fit a desktop.
    readonly property int controlHeightSmall: 28
    readonly property int controlHeight:      32
    readonly property int controlHeightLarge: 40

    /// The smallest a thing can be and still be reliably hit with a pointer.
    /// Nothing interactive may be smaller in either direction.
    readonly property int minimumTouchTarget: 24

    // Section 24. Rows are compact because desktop applications are good at
    // dense data and bad at making people scroll for it.
    readonly property int rowHeightCompact:     32
    readonly property int rowHeight:            36
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
