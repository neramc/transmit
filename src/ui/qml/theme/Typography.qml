pragma Singleton

import QtQuick

/// Type scale and weights.
///
/// The interface font is the platform's own, so the application looks like it
/// belongs on the desktop it is running on rather than importing a typeface
/// from nowhere.
QtObject {
    readonly property string family: Qt.application.font.family

    /// Fixed-width, for paths and other text where alignment carries meaning.
    readonly property string monoFamily: Qt.platform.os === "windows" ? "Cascadia Mono, Consolas"
                                       : Qt.platform.os === "osx"     ? "SF Mono, Menlo"
                                                                      : "monospace"

    // A restrained scale: five sizes is enough to build a hierarchy, and more
    // only makes it harder to keep consistent.
    readonly property int display: 26
    readonly property int title:   19
    readonly property int heading: 15
    readonly property int body:    13
    readonly property int small:   12
    readonly property int caption: 11

    readonly property int regular:  Font.Normal
    readonly property int medium:   Font.Medium
    readonly property int semiBold: Font.DemiBold

    /// Line heights as multipliers, for text that wraps.
    readonly property real lineHeightTight:  1.2
    readonly property real lineHeightNormal: 1.35
    readonly property real lineHeightLoose:  1.5
}
