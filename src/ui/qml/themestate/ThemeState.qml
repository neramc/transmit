pragma Singleton

import QtQuick

/// The one piece of theme state that changes at runtime.
///
/// It is kept apart from the palettes so those can be plain constants that
/// read from it, rather than every design-system file having to know how the
/// user's preference is stored. Main.qml is the only writer.
QtObject {
    /// "light", "dark" or "system".
    property string mode: "system"

    /// What the platform's own colour scheme reports, when the mode follows it.
    property bool systemPrefersDark: false

    readonly property bool dark: mode === "dark"
                                 || (mode === "system" && systemPrefersDark)

    /// Which desktop's measurements to use: "windows11", "windows10", "macos",
    /// "gnome", "kde", "xfce", "cosmic", or "default" for this design system's
    /// own. Written by Main.qml from what the platform reported.
    ///
    /// Kept here rather than read from the backend by each theme file, so the
    /// design system stays a set of constants that depend on nothing but this
    /// one object - which is also what lets a test set it and see the whole
    /// interface follow.
    property string platform: "default"

    /// The desktop's own highlight colour, when it could be read. Left
    /// invalid otherwise, and the brand colour is used: an accent guessed
    /// wrong is worse than one chosen on purpose.
    property color systemAccent: "transparent"

    readonly property bool hasSystemAccent: systemAccent.a > 0

    /// Windows puts the button that goes ahead on the left of the pair;
    /// macOS and GNOME put it on the right. Getting this backwards is how a
    /// person cancels something they meant to confirm.
    readonly property bool acceptFirst: platform === "windows11"
                                        || platform === "windows10"
                                        || platform === "kde"
                                        || platform === "xfce"
                                        || platform === "default"
}
