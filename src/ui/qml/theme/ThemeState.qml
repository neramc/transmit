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
}
