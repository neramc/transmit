pragma Singleton

import QtQuick

/// Control metrics.
///
/// Desktop controls want a consistent height above all else - a row of a
/// button, a field and a combo box should line up without anyone nudging
/// margins to make it happen.
QtObject {
    readonly property int controlHeight:      32
    readonly property int controlHeightLarge: 40
    readonly property int controlHeightSmall: 26

    readonly property int iconSize:      16
    readonly property int iconSizeLarge: 22

    readonly property int sidebarWidth:  220
    readonly property int toolbarHeight: 48
    readonly property int statusBarHeight: 28

    /// Text stops growing past this: a line much longer than about 90
    /// characters is measurably harder to read.
    readonly property int maxContentWidth: 980
}
