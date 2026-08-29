import QtQuick
import QtQuick.Layouts
import Transmit.Theme

/// Shows where the user is in a wizard and how much is left. Steps already
/// completed stay visible so the sequence is legible at a glance.
RowLayout {
    id: stepper

    property var steps: []
    property int currentStep: 0

    spacing: Spacing.sm
    Accessible.role: Accessible.ProgressBar
    Accessible.name: qsTr("Step %1 of %2").arg(currentStep + 1).arg(steps.length)

    Repeater {
        model: stepper.steps

        RowLayout {
            id: stepRow

            required property int index
            required property string modelData

            spacing: Spacing.sm
            readonly property bool done: stepRow.index < stepper.currentStep
            readonly property bool active: stepRow.index === stepper.currentStep

            Rectangle {
                width: 22
                height: 22
                radius: width / 2
                color: stepRow.done ? Colors.accent
                     : stepRow.active ? Colors.accentSubtle : "transparent"
                border.width: stepRow.active || stepRow.done ? 0 : Elevation.borderWidth
                border.color: Colors.border

                Behavior on color {
                    ColorAnimation { duration: Motion.panel; easing.type: Motion.easing }
                }

                Text {
                    anchors.centerIn: parent
                    text: stepRow.done ? "\u2713" : (stepRow.index + 1)
                    font.family: Typography.family
                    font.pixelSize: Typography.caption
                    font.weight: Typography.semiBold
                    color: stepRow.done ? Colors.textOnAccent
                         : stepRow.active ? Colors.accent
                                          : Colors.textDisabled
                }
            }

            Text {
                text: stepRow.modelData
                color: stepRow.active ? Colors.textPrimary : Colors.textSecondary
                font.family: Typography.family
                font.pixelSize: Typography.small
                font.weight: stepRow.active ? Typography.medium : Typography.regular
            }

            Rectangle {
                Layout.preferredWidth: Spacing.lg
                height: Elevation.borderWidth
                color: Colors.border
                visible: stepRow.index < stepper.steps.length - 1
            }
        }
    }
}
