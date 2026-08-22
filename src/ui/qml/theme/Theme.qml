pragma Singleton

import QtQuick

/// The single source of visual truth. Every colour, size, duration and radius
/// used anywhere in the interface comes from here, so the whole application can
/// be restyled - or switched between light and dark - in one place.
QtObject {
    id: theme

    // "light", "dark" or "system"
    property string mode: "system"

    readonly property bool dark: mode === "dark"
                                 || (mode === "system" && systemPrefersDark)

    /// Qt reports the platform's colour scheme; on a desktop that has no
    /// preference this stays false and the light palette is used.
    property bool systemPrefersDark: false

    // ----------------------------------------------------------- colours
    readonly property color background:      dark ? "#16181d" : "#f6f7f9"
    readonly property color surface:         dark ? "#1e2128" : "#ffffff"
    readonly property color surfaceElevated: dark ? "#262a33" : "#ffffff"
    readonly property color surfaceSunken:   dark ? "#121419" : "#eceef2"
    readonly property color border:          dark ? "#333845" : "#dfe3ea"
    readonly property color borderStrong:    dark ? "#454c5c" : "#c3cad6"

    readonly property color textPrimary:   dark ? "#eceef4" : "#1a1d24"
    readonly property color textSecondary: dark ? "#a4abbb" : "#5d6577"
    readonly property color textDisabled:  dark ? "#646b7c" : "#a0a7b4"
    readonly property color textOnAccent:  "#ffffff"

    readonly property color accent:        dark ? "#5b8def" : "#2f6fed"
    readonly property color accentHover:   dark ? "#6f9cf3" : "#215ed6"
    readonly property color accentPressed: dark ? "#4a7ad9" : "#1a4fba"
    readonly property color accentSubtle:  dark ? "#1d2942" : "#e8f0fe"

    readonly property color success:       dark ? "#4cb782" : "#1f9254"
    readonly property color successSubtle: dark ? "#16281f" : "#e6f5ed"
    readonly property color warning:       dark ? "#d99b3a" : "#b9740a"
    readonly property color warningSubtle: dark ? "#2b2415" : "#fdf3e2"
    readonly property color error:         dark ? "#e56b6b" : "#c93636"
    readonly property color errorSubtle:   dark ? "#2d1a1c" : "#fbeaea"
    readonly property color info:          dark ? "#5b8def" : "#2f6fed"
    readonly property color infoSubtle:    dark ? "#1d2942" : "#e8f0fe"

    readonly property color overlay: dark ? "#000000b3" : "#1a1d2466"

    // -------------------------------------------------------- typography
    readonly property int fontSizeDisplay: 26
    readonly property int fontSizeTitle:   19
    readonly property int fontSizeHeading: 15
    readonly property int fontSizeBody:    13
    readonly property int fontSizeSmall:   12
    readonly property int fontSizeCaption: 11

    readonly property int weightRegular:  Font.Normal
    readonly property int weightMedium:   Font.Medium
    readonly property int weightSemiBold: Font.DemiBold

    /// The platform's own interface font, so the application looks native
    /// rather than importing a web font that belongs nowhere.
    readonly property string fontFamily: Qt.application.font.family
    readonly property string monoFamily: Qt.platform.os === "windows" ? "Consolas"
                                       : Qt.platform.os === "osx"     ? "SF Mono"
                                                                      : "monospace"

    // ----------------------------------------------------------- spacing
    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 16
    readonly property int spacingXl: 24
    readonly property int spacing2xl: 32
    readonly property int spacing3xl: 48

    // ------------------------------------------------------------ radius
    readonly property int radiusSm: 4
    readonly property int radiusMd: 6
    readonly property int radiusLg: 10
    readonly property int radiusPill: 999

    // ------------------------------------------------------------ sizing
    readonly property int controlHeight:      32
    readonly property int controlHeightLarge: 40
    readonly property int iconSize:           16
    readonly property int iconSizeLarge:      22
    readonly property int sidebarWidth:       220
    readonly property int borderWidth:        1
    readonly property int focusRingWidth:     2

    // ------------------------------------------------------------ motion
    // Short enough to feel instant, long enough to be followed by the eye.
    readonly property int durationHover:  120
    readonly property int durationPress:  80
    readonly property int durationPanel:  200
    readonly property int durationDialog: 200
    readonly property int easing: Easing.OutCubic
}
