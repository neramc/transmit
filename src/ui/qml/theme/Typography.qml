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
    ///
    /// One name, and one that is actually installed. font.family takes a
    /// single family, so a comma-separated list is read as a family with a
    /// comma in its name: Qt then reports it missing, spends time searching
    /// the aliases, and substitutes something. font.families would express a
    /// fallback chain but arrived after the Qt version this project supports.
    ///
    /// So: Menlo, which has shipped with macOS since 10.6 - SF Mono comes with
    /// Xcode and cannot be relied on. Consolas, which has shipped with Windows
    /// since Vista. And on Linux the fontconfig alias, which is guaranteed to
    /// resolve to whatever is installed.
    readonly property string monoFamily: Qt.platform.os === "windows" ? "Consolas"
                                       : Qt.platform.os === "osx"     ? "Menlo"
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
