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
    implicitHeight: layout.implicitHeight + Theme.spacingLg * 2
    activeFocusOnTab: true

    Accessible.role: Accessible.RadioButton
    Accessible.name: title
    Accessible.description: description
    Accessible.checked: selected
    Keys.onReturnPressed: option.clicked()
    Keys.onSpacePressed: option.clicked()

    Rectangle {
        anchors.fill: parent
        anchors.margins: -Theme.focusRingWidth - 1
        radius: parent.radius + Theme.focusRingWidth
        color: "transparent"
        border.width: Theme.focusRingWidth
        border.color: Theme.accent
        visible: option.activeFocus
    }

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        // A ring rather than a tick: this is a choice, not a checklist.
        Rectangle {
            Layout.alignment: Qt.AlignTop
            width: 18
            height: 18
            radius: 9
            color: "transparent"
            border.width: 2
            border.color: option.selected ? Theme.accent : Theme.borderStrong

            Rectangle {
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: Theme.accent
                visible: option.selected
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    text: option.title
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Theme.weightSemiBold
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
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
                lineHeight: 1.3
            }
        }

        Text {
            text: option.trailingText
            visible: option.trailingText !== ""
            Layout.alignment: Qt.AlignVCenter
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSmall
        }
    }
}
