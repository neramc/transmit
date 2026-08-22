import QtQuick
import QtQuick.Layouts
import Transmit.Theme

/// Shows where the user is in a wizard and how much is left. Steps already
/// completed stay visible so the sequence is legible at a glance.
RowLayout {
    id: stepper

    property var steps: []
    property int currentStep: 0

    spacing: Theme.spacingSm
    Accessible.role: Accessible.ProgressBar
    Accessible.name: qsTr("Step %1 of %2").arg(currentStep + 1).arg(steps.length)

    Repeater {
        model: stepper.steps

        RowLayout {
            id: stepRow

            required property int index
            required property string modelData

            spacing: Theme.spacingSm
            readonly property bool done: stepRow.index < stepper.currentStep
            readonly property bool active: stepRow.index === stepper.currentStep

            Rectangle {
                width: 22
                height: 22
                radius: 11
                color: stepRow.done ? Theme.accent
                     : stepRow.active ? Theme.accentSubtle : "transparent"
                border.width: stepRow.active || stepRow.done ? 0 : Theme.borderWidth
                border.color: Theme.border

                Behavior on color {
                    ColorAnimation { duration: Theme.durationPanel; easing.type: Theme.easing }
                }

                Text {
                    anchors.centerIn: parent
                    text: stepRow.done ? "\u2713" : (stepRow.index + 1)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeCaption
                    font.weight: Theme.weightSemiBold
                    color: stepRow.done ? Theme.textOnAccent
                         : stepRow.active ? Theme.accent
                                          : Theme.textDisabled
                }
            }

            Text {
                text: stepRow.modelData
                color: stepRow.active ? Theme.textPrimary : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSmall
                font.weight: stepRow.active ? Theme.weightMedium : Theme.weightRegular
            }

            Rectangle {
                Layout.preferredWidth: Theme.spacingLg
                height: Theme.borderWidth
                color: Theme.border
                visible: stepRow.index < stepper.steps.length - 1
            }
        }
    }
}
