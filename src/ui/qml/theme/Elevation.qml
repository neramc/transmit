pragma Singleton

import QtQuick

/// Depth, expressed mostly through borders rather than shadows.
///
/// A desktop application earns its sense of layering from crisp edges; large
/// soft shadows belong to a different medium and make everything look like it
/// is floating.
///
/// Deliberately holds no colours. A singleton here that read Colors would
/// import its own module and the engine would reject the pair as a cycle -
/// callers reach for Colors.shadow themselves.
QtObject {
    readonly property int borderWidth: 1
    readonly property int focusRingWidth: 2

    /// Shadows are reserved for things that genuinely float above the window:
    /// popups, menus and dialogs.
    readonly property int popupShadowRadius: 24
    readonly property int popupShadowOffset: 4

    readonly property int dialogShadowRadius: 40
    readonly property int dialogShadowOffset: 8
}
