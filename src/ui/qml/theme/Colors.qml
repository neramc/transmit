pragma Singleton

import QtQuick
import Transmit.Theme

/// The palette, in both schemes.
///
/// Colours are named for what they mean rather than what they look like, so a
/// component asks for `Colors.textSecondary` and gets something legible in
/// either scheme without knowing which one is active. Nothing outside this
/// file writes a colour literal; scripts/check-design-tokens.sh enforces it.
///
/// The neutrals follow the ramp in docs/design.md section 8. The accent is the
/// violet from the application icon, so the window and the thing in the dock
/// are recognisably the same product - and because a brand colour used for
/// meaning is the one place section 9 allows colour to carry weight.
///
/// Every pair a reader has to tell apart is checked by
/// scripts/check-contrast.py, which parses this file. Changing a value here
/// without running it is how an interface ends up unreadable for somebody who
/// never gets to complain about it.
QtObject {
    readonly property bool dark: ThemeState.dark

    // --------------------------------------------------------- surfaces
    // The window ground is faintly tinted and cards sit on top of it in white.
    // The other way round - white window, grey cards - makes every panel look
    // like a hole rather than a thing.
    readonly property color background:      dark ? "#0C0C10" : "#F8F8FA"
    readonly property color surface:         dark ? "#18181B" : "#FFFFFF"
    readonly property color surfaceElevated: dark ? "#202023" : "#FFFFFF"
    readonly property color surfaceSunken:   dark ? "#09090B" : "#F1F1F4"
    readonly property color surfaceHover:    dark ? "#232327" : "#F1F1F4"
    readonly property color surfacePressed:  dark ? "#2A2A2F" : "#E7E7EB"

    // ---------------------------------------------------------- borders
    // `border` separates one region from another and is decorative: the fills
    // either side already say where the edge is. `borderStrong` is the outline
    // of a control - an input, a combo box - where the border is the only thing
    // saying the control is there, so it has to clear 3:1 against both surfaces
    // it can sit on. That is why it is so much darker than it looks like it
    // ought to be.
    readonly property color border:       dark ? "#27272A" : "#E4E4E7"
    readonly property color borderStrong: dark ? "#6A6A75" : "#8E8E99"

    // ------------------------------------------------------------- text
    readonly property color textPrimary:   dark ? "#FAFAFA" : "#18181B"
    readonly property color textSecondary: dark ? "#A1A1AA" : "#65656E"
    // WCAG puts no minimum on the text of a control that is switched off, but
    // section 10 asks for readable disabled states and somebody has to be able
    // to tell what the button would say if it were on.
    readonly property color textDisabled:  dark ? "#5F5F6A" : "#9A9AA4"

    // ----------------------------------------------------------- accent
    // `accent` is a fill and always carries `textOnAccent`. `accentText` is
    // the accent used as text or an icon straight on the background, which
    // needs more contrast than a fill does and so is a lighter shade in the
    // dark scheme.
    readonly property color accent:        dark ? "#A71AF9" : "#A200FF"
    readonly property color accentHover:   dark ? "#AF2EFA" : "#8F00E0"
    readonly property color accentPressed: dark ? "#9306E5" : "#7A00BF"
    readonly property color accentSubtle:  dark ? "#231033" : "#F6EBFF"
    readonly property color accentBorder:  dark ? "#4A1E6B" : "#E0C2FA"
    readonly property color accentText:    dark ? "#C875FC" : "#8F00E0"
    readonly property color textOnAccent:  "#FFFFFF"

    // ------------------------------------------------------------ status
    // Distinct from the accent, as section 9 requires: an interface where the
    // brand colour also means "information" cannot say either thing clearly.
    // Each has a subtle background so a message can be tinted without its text
    // losing contrast against it.
    readonly property color success:       dark ? "#3FB37A" : "#136B34"
    readonly property color successSubtle: dark ? "#0F2019" : "#E7F6EC"
    readonly property color successBorder: dark ? "#1D4034" : "#B7E0C4"

    readonly property color warning:       dark ? "#D9A03C" : "#9A6206"
    readonly property color warningSubtle: dark ? "#231C10" : "#FDF3E2"
    readonly property color warningBorder: dark ? "#463A1E" : "#EDD3A3"

    readonly property color error:         dark ? "#F06565" : "#C02626"
    readonly property color errorSubtle:   dark ? "#251416" : "#FCEBEB"
    readonly property color errorBorder:   dark ? "#4A2225" : "#F3C2C2"

    readonly property color info:          dark ? "#5AA9F5" : "#1B62B8"
    readonly property color infoSubtle:    dark ? "#0E1B27" : "#E8F1FC"
    readonly property color infoBorder:    dark ? "#1E3852" : "#BBD8F5"

    // ------------------------------------------------------------ misc
    readonly property color overlay:   dark ? "#000000C0" : "#18181BA6"
    readonly property color focusRing: dark ? "#C875FC" : "#A200FF"
    readonly property color shadow:    dark ? "#00000080" : "#18181B26"

    /// The brand violet at full strength, for the one or two places that are
    /// showing the product's identity rather than an interactive state.
    readonly property color brand: "#A200FF"
}
