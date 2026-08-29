import QtQuick
import QtQuick.Controls.Basic
import Transmit.Theme

ProgressBar {
    id: control

    /// Shows a moving band when the total is not yet known, which is the case
    /// while a capture is still counting files.
    property bool unknownTotal: false

    implicitHeight: 6
    indeterminate: unknownTotal

    background: Rectangle {
        implicitHeight: control.implicitHeight
        radius: Radius.pill
        color: Colors.surfaceSunken
    }

    contentItem: Item {
        implicitHeight: control.implicitHeight

        Rectangle {
            width: control.indeterminate ? parent.width * 0.3
                                         : control.visualPosition * parent.width
            height: parent.height
            radius: Radius.pill
            color: Colors.accent

            Behavior on width {
                enabled: !control.indeterminate
                NumberAnimation { duration: Motion.panel; easing.type: Motion.easing }
            }

            // Stopped rather than sped up when motion is reduced. A bar that
            // sweeps back and forth for as long as the work lasts is exactly
            // what somebody who has asked for less movement is asking about.
            SequentialAnimation on x {
                running: control.indeterminate && control.visible && !Motion.reduced
                loops: Animation.Infinite
                NumberAnimation { from: 0; to: control.width * 0.7; duration: Motion.loop; easing.type: Easing.InOutQuad }
                NumberAnimation { from: control.width * 0.7; to: 0; duration: Motion.loop; easing.type: Easing.InOutQuad }
            }
        }
    }
}
