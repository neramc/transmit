import QtQuick
import QtQuick.Controls.Basic
import Transmit.Theme

TextField {
    id: control

    property string label: ""
    property string helperText: ""
    property bool hasError: false

    implicitHeight: Sizing.controlHeightLarge
    leftPadding: Spacing.md
    rightPadding: Spacing.md
    font.family: Typography.family
    font.pixelSize: Typography.body
    color: Colors.textPrimary
    placeholderTextColor: Colors.textDisabled
    selectionColor: Colors.accent
    selectedTextColor: Colors.textOnAccent

    Accessible.role: Accessible.EditableText
    Accessible.name: label !== "" ? label : placeholderText
    Accessible.description: helperText

    background: Rectangle {
        radius: Radius.md
        color: control.enabled ? Colors.surface : Colors.surfaceSunken
        border.width: control.activeFocus ? Elevation.focusRingWidth : Elevation.borderWidth
        border.color: control.hasError ? Colors.error
                    : control.activeFocus ? Colors.accent
                                          : Colors.border

        Behavior on border.color {
            ColorAnimation { duration: Motion.hover; easing.type: Motion.easing }
        }
    }
}
