import QtQuick
import QtQuick.Layouts
import Transmit.Theme

/// A card the user picks from a list: a capture profile, a drive, a policy.
/// The whole card is the target, and it is reachable from the keyboard.
AppCard {
    id: option

    property string title: ""
    property string description: ""
    property string trailingText: ""
    property string badgeText: ""
    property string badgeTone: "neutral"

    interactive: true
    implicitHeight: layout.implicitHeight + Spacing.lg * 2
    activeFocusOnTab: true

    Accessible.role: Accessible.RadioButton
    Accessible.name: title
    Accessible.description: description
    Accessible.checked: selected
    Keys.onReturnPressed: option.clicked()
    Keys.onSpacePressed: option.clicked()

    Rectangle {
        anchors.fill: parent
        anchors.margins: -Elevation.focusRingWidth - 1
        radius: parent.radius + Elevation.focusRingWidth
        color: "transparent"
        border.width: Elevation.focusRingWidth
        border.color: Colors.accent
        visible: option.activeFocus
    }

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Spacing.lg
        spacing: Spacing.md

        // A ring rather than a tick: this is a choice, not a checklist.
        Rectangle {
            Layout.alignment: Qt.AlignTop
            implicitWidth: 18
            implicitHeight: 18
            radius: width / 2
            color: "transparent"
            border.width: Elevation.selectionRingWidth
            border.color: option.selected ? Colors.accent : Colors.borderStrong

            Rectangle {
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: width / 2
                color: Colors.accent
                visible: option.selected
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Spacing.xs

            RowLayout {
                Layout.fillWidth: true
                spacing: Spacing.sm

                Text {
                    text: option.title
                    color: Colors.textPrimary
                    font.family: Typography.family
                    font.pixelSize: Typography.body
                    font.weight: Typography.semiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                AppBadge {
                    text: option.badgeText
                    tone: option.badgeTone
                    visible: option.badgeText !== ""
                }
            }

            Text {
                text: option.description
                visible: option.description !== ""
                Layout.fillWidth: true
                color: Colors.textSecondary
                font.family: Typography.family
                font.pixelSize: Typography.small
                wrapMode: Text.WordWrap
                lineHeight: 1.3
            }
        }

        Text {
            text: option.trailingText
            visible: option.trailingText !== ""
            Layout.alignment: Qt.AlignVCenter
            color: Colors.textSecondary
            font.family: Typography.family
            font.pixelSize: Typography.small
        }
    }
}
