import QtQuick
import QtQuick.Controls.Basic
import Transmit.Theme

TextField {
    id: control

    property string label: ""
    property string helperText: ""
    property bool hasError: false

    implicitHeight: Theme.controlHeightLarge
    leftPadding: Theme.spacingMd
    rightPadding: Theme.spacingMd
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeBody
    color: Theme.textPrimary
    placeholderTextColor: Theme.textDisabled
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent

    Accessible.role: Accessible.EditableText
    Accessible.name: label !== "" ? label : placeholderText
    Accessible.description: helperText

    background: Rectangle {
        radius: Theme.radiusMd
        color: control.enabled ? Theme.surface : Theme.surfaceSunken
        border.width: control.activeFocus ? Theme.focusRingWidth : Theme.borderWidth
        border.color: control.hasError ? Theme.error
                    : control.activeFocus ? Theme.accent
                                          : Theme.border

        Behavior on border.color {
            ColorAnimation { duration: Theme.durationHover; easing.type: Theme.easing }
        }
    }
}
