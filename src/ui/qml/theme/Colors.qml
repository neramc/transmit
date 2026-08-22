pragma Singleton

import QtQuick
import Transmit.Theme

/// The palette, in both schemes.
///
/// Colours are named for what they mean rather than what they look like, so a
/// component asks for `Colors.textSecondary` and gets something legible in
/// either scheme without knowing which one is active.
QtObject {
    readonly property bool dark: ThemeState.dark

    // --------------------------------------------------------- surfaces
    readonly property color background:      dark ? "#15171c" : "#f7f8fa"
    readonly property color surface:         dark ? "#1c1f26" : "#ffffff"
    readonly property color surfaceElevated: dark ? "#232730" : "#ffffff"
    readonly property color surfaceSunken:   dark ? "#111318" : "#eef0f4"
    readonly property color surfaceHover:    dark ? "#272c36" : "#f2f4f8"

    // ---------------------------------------------------------- borders
    readonly property color border:       dark ? "#2e333f" : "#e1e5ec"
    readonly property color borderStrong: dark ? "#3f4655" : "#c6ccd8"

    // ------------------------------------------------------------- text
    readonly property color textPrimary:   dark ? "#eef0f5" : "#171a20"
    readonly property color textSecondary: dark ? "#a2aabb" : "#5b6472"
    readonly property color textDisabled:  dark ? "#5f6879" : "#a3abb8"
    readonly property color textOnAccent:  "#ffffff"

    // ----------------------------------------------------------- accent
    readonly property color accent:        dark ? "#5c8cf0" : "#2f6bea"
    readonly property color accentHover:   dark ? "#7099f3" : "#245ad3"
    readonly property color accentPressed: dark ? "#4a79dc" : "#1c4cb6"
    readonly property color accentSubtle:  dark ? "#1b2740" : "#e9f0fe"
    readonly property color accentBorder:  dark ? "#2f4470" : "#bcd2fb"

    // ------------------------------------------------------- status
    // Each has a subtle background so a message can be tinted without the
    // text losing contrast against it.
    readonly property color success:       dark ? "#4bb583" : "#1c8c52"
    readonly property color successSubtle: dark ? "#14251d" : "#e4f4ec"
    readonly property color warning:       dark ? "#d9a03c" : "#a8700a"
    readonly property color warningSubtle: dark ? "#282013" : "#fcf3e3"
    readonly property color error:         dark ? "#e56d6d" : "#c53434"
    readonly property color errorSubtle:   dark ? "#2a191b" : "#fbebeb"
    readonly property color info:          dark ? "#5c8cf0" : "#2f6bea"
    readonly property color infoSubtle:    dark ? "#1b2740" : "#e9f0fe"

    // ------------------------------------------------------------ misc
    readonly property color overlay:   dark ? "#000000b8" : "#141920b0"
    readonly property color focusRing: dark ? "#5c8cf0" : "#2f6bea"
    readonly property color shadow:    dark ? "#00000066" : "#1a1f2a1f"
}
