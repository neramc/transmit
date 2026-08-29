pragma Singleton

import QtQuick

/// Durations and easing.
///
/// The numbers come from what the interaction is: a hover has to keep up with
/// the pointer, a panel has to be followed by the eye. Anything longer than a
/// panel transition starts to feel like waiting.
QtObject {
    // Section 28, at the fast end of each range. Everything here is over
    // before it can be waited for.
    readonly property int press:  80    ///< instant feedback
    readonly property int hover:  120
    readonly property int small:  140   ///< a colour or a size changing
    readonly property int panel:  180
    readonly property int dialog: 180
    readonly property int page:   220   ///< one page replacing another

    /// For a value that settles rather than snaps - progress bars, counters.
    readonly property int settle: 320

    /// One turn of a spinner, or one sweep of an indeterminate progress bar.
    /// Longer than anything else here because it repeats: a fast loop reads as
    /// agitation rather than as work being done.
    readonly property int loop: 900

    readonly property int easing:      Easing.OutCubic
    readonly property int easingEnter:  Easing.OutCubic
    readonly property int easingExit:   Easing.InCubic
    readonly property int easingEmphasis: Easing.OutBack

    /// Honours the accessibility preference: someone who has asked for less
    /// motion should not have to watch anything move.
    property bool reduced: false
    function duration(base) { return reduced ? 0 : base }
}
