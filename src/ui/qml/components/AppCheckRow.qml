import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Transmit.Theme

/// A switch with a label and an explanation, for options where the consequence
/// matters enough to be spelled out next to the control.
RowLayout {
    id: row

    property string label: ""
    property string description: ""
    property alias checked: toggle.checked
    property alias enabledControl: toggle.enabled

    Layout.fillWidth: true
    spacing: Spacing.md

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Spacing.s4

        Text {
            text: row.label
            Layout.fillWidth: true
            color: toggle.enabled ? Colors.textPrimary : Colors.textDisabled
            font.family: Typography.family
            font.pixelSize: Typography.body
            wrapMode: Text.WordWrap
        }

        Text {
            text: row.description
            visible: row.description !== ""
            Layout.fillWidth: true
            color: Colors.textSecondary
            font.family: Typography.family
            font.pixelSize: Typography.caption
            wrapMode: Text.WordWrap
            lineHeight: 1.3
        }
    }

    Switch {
        id: toggle

        // A definite size with a minimum to match. Without it the layout gives
        // the wrapping description its full unwrapped width and squeezes the
        // switch down to a sliver at the edge of the panel - present, but far
        // too narrow to see or to hit.
        implicitWidth: 44
        implicitHeight: 24
        padding: 0

        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: implicitWidth
        Layout.minimumWidth: implicitWidth

        Accessible.name: row.label
        Accessible.description: row.description

        indicator: Rectangle {
            implicitWidth: 40
            implicitHeight: 22
            x: (toggle.width - width) / 2
            y: (toggle.height - height) / 2
            radius: Radius.pill
            color: toggle.checked ? Colors.accent : Colors.borderStrong
            opacity: toggle.enabled ? 1.0 : 0.5

            Behavior on color {
                ColorAnimation {
                    duration: Motion.duration(Motion.hover)
                    easing.type: Motion.easing
                }
            }

            Rectangle {
                x: toggle.checked ? parent.width - width - 3 : 3
                y: 3
                width: 16
                height: 16
                radius: width / 2
                color: Colors.textOnAccent

                Behavior on x {
                    NumberAnimation {
                        duration: Motion.duration(Motion.hover)
                        easing.type: Motion.easing
                    }
                }
            }
        }

        contentItem: Item {}
    }
}
