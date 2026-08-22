import QtQuick
import QtQuick.Controls.Basic
import Transmit.Theme

/// A tooltip for the detail that would clutter the interface if it were always
/// on screen. It never carries anything the user must read - what a control
/// does is said by its label.
ToolTip {
    id: control

    delay: 500
    timeout: 6000
    padding: Spacing.sm

    contentItem: Text {
        text: control.text
        color: Colors.textPrimary
        font.family: Typography.family
        font.pixelSize: Typography.small
        wrapMode: Text.WordWrap
        // Long enough to hold a sentence, short enough to stay readable.
        width: Math.min(implicitWidth, 320)
    }

    background: Rectangle {
        radius: Radius.sm
        color: Colors.surfaceElevated
        border.width: Elevation.borderWidth
        border.color: Colors.border
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"; from: 0; to: 1
            duration: Motion.duration(Motion.hover); easing.type: Motion.easingEnter
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"; from: 1; to: 0
            duration: Motion.duration(Motion.press); easing.type: Motion.easingExit
        }
    }
}
