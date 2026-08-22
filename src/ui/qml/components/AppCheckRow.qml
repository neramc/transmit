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
        spacing: 2

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
        Layout.alignment: Qt.AlignVCenter

        Accessible.name: row.label
        Accessible.description: row.description

        indicator: Rectangle {
            implicitWidth: 40
            implicitHeight: 22
            x: toggle.leftPadding
            y: parent.height / 2 - height / 2
            radius: Radius.pill
            color: toggle.checked ? Colors.accent : Colors.borderStrong
            opacity: toggle.enabled ? 1.0 : 0.5

            Behavior on color {
                ColorAnimation { duration: Motion.hover; easing.type: Motion.easing }
            }

            Rectangle {
                x: toggle.checked ? parent.width - width - 3 : 3
                y: 3
                width: 16
                height: 16
                radius: 8
                color: "#ffffff"

                Behavior on x {
                    NumberAnimation { duration: Motion.hover; easing.type: Motion.easing }
                }
            }
        }

        contentItem: Item {}
    }
}
