import QtQuick
import Transmit.Theme

/// A grey block standing in for content that has not arrived.
///
/// docs/design.md section 21 prefers these to a spinner for a large content
/// area, and the reason is that a skeleton says how much is coming and where it
/// will be, so the page does not jump when it lands. A spinner says only that
/// something is happening.
///
/// The shimmer stops when the user has asked for less motion; a rectangle that
/// pulses forever is exactly what that setting is about.
Rectangle {
    id: skeleton

    /// Rows drawn one under another, each a little different in width so the
    /// block reads as text rather than as a table.
    property int lines: 1
    property real lastLineFraction: 0.6

    implicitHeight: lines * Sizing.iconSize + (lines - 1) * Spacing.s8
    color: "transparent"

    Accessible.role: Accessible.Indicator
    Accessible.name: qsTr("Loading")

    Column {
        anchors.fill: parent
        spacing: Spacing.s8

        Repeater {
            model: skeleton.lines

            Rectangle {
                required property int index

                width: index === skeleton.lines - 1 && skeleton.lines > 1
                       ? skeleton.width * skeleton.lastLineFraction
                       : skeleton.width
                height: Sizing.iconSize
                radius: Radius.chip
                color: Colors.surfaceHover

                SequentialAnimation on opacity {
                    running: skeleton.visible && !Motion.reduced
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.45; duration: Motion.loop / 2; easing.type: Easing.InOutQuad }  // token-exempt: the animation is stopped outright by running: !Motion.reduced
                    NumberAnimation { from: 0.45; to: 1.0; duration: Motion.loop / 2; easing.type: Easing.InOutQuad }  // token-exempt: as above
                }
            }
        }
    }
}
