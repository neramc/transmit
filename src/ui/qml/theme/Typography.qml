pragma Singleton

import QtQuick

/// Type scale and weights.
///
/// The interface font is the platform's own, so the application looks like it
/// belongs on the desktop it is running on rather than importing a typeface
/// from nowhere.
QtObject {
    readonly property string family: Application.font.family

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

    // The hierarchy from docs/design.md section 7, at the smaller end of each
    // range it gives. A desktop tool wants to be information-dense (section
    // 36), and the ranges there are written for the whole spread of desktop
    // applications rather than for one that shows file lists and progress.
    //
    // Six sizes. Every one of them earns its place by appearing in the
    // hierarchy of section 35: a page title, the sentence under it, the
    // heading of a section, the body, the quieter half of a key-value row, and
    // a caption. A seventh would only be harder to keep consistent.
    readonly property int display:      30   ///< the one number on a result screen
    readonly property int pageTitle:    24
    readonly property int sectionTitle: 18
    readonly property int body:         14
    readonly property int secondary:    13
    readonly property int caption:      12

    readonly property int regular:  Font.Normal
    readonly property int medium:   Font.Medium
    readonly property int semiBold: Font.DemiBold
    readonly property int bold:     Font.Bold

    // Older names. `title` and `heading` mapped onto the two sizes above them
    // rather than being dropped, so a page that has not been revisited still
    // reads sensibly - just at the new scale.
    readonly property int title:   pageTitle
    readonly property int heading: sectionTitle
    readonly property int small:   secondary

    /// Line heights as multipliers, for text that wraps.
    readonly property real lineHeightTight:  1.2
    readonly property real lineHeightNormal: 1.35
    readonly property real lineHeightLoose:  1.5
}
