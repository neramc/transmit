import QtQuick
import QtQuick.Controls.Basic
import Transmit.Theme

/// A scrollbar that stays out of the way until it is needed, then widens under
/// the pointer so it is easy to grab.
ScrollBar {
    id: control

    padding: 2  // token-exempt: the inset of the handle inside its own track, not page spacing
    minimumSize: 0.08

    contentItem: Rectangle {
        implicitWidth: control.pressed || control.hovered ? 8 : 5
        implicitHeight: control.pressed || control.hovered ? 8 : 5
        radius: width / 2
        color: control.pressed ? Colors.textSecondary : Colors.borderStrong
        opacity: control.policy === ScrollBar.AlwaysOn || control.active ? 1.0 : 0.0

        Behavior on implicitWidth {
            NumberAnimation { duration: Motion.duration(Motion.hover); easing.type: Motion.easing }
        }
        Behavior on implicitHeight {
            NumberAnimation { duration: Motion.duration(Motion.hover); easing.type: Motion.easing }
        }
        Behavior on opacity {
            NumberAnimation { duration: Motion.duration(Motion.panel) }
        }
    }

    background: Item {}
}
