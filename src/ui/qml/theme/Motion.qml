pragma Singleton

import QtQuick

/// Durations and easing.
///
/// The numbers come from what the interaction is: a hover has to keep up with
/// the pointer, a panel has to be followed by the eye. Anything longer than a
/// panel transition starts to feel like waiting.
QtObject {
    readonly property int hover:  120
    readonly property int press:  80
    readonly property int panel:  200
    readonly property int dialog: 200

    /// For a value that settles rather than snaps - progress bars, counters.
    readonly property int settle: 320

    readonly property int easing:      Easing.OutCubic
    readonly property int easingEnter:  Easing.OutCubic
    readonly property int easingExit:   Easing.InCubic
    readonly property int easingEmphasis: Easing.OutBack

    /// Honours the accessibility preference: someone who has asked for less
    /// motion should not have to watch anything move.
    property bool reduced: false
    function duration(base) { return reduced ? 0 : base }
}
