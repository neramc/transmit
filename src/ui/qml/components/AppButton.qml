import QtQuick
import QtQuick.Controls.Basic
import Transmit.Theme

/// The application's button. Variants exist so intent is expressed once, at the
/// call site, instead of every caller restating colours.
Button {
    id: control

    /// "primary", "secondary", "ghost" or "danger"
    property string variant: "secondary"
    property bool destructive: variant === "danger"

    implicitHeight: Theme.controlHeightLarge
    implicitWidth: Math.max(96, contentItem.implicitWidth + Theme.spacingXl * 2)
    padding: Theme.spacingLg
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeBody
    font.weight: variant === "primary" ? Theme.weightSemiBold : Theme.weightMedium

    Accessible.role: Accessible.Button
    Accessible.name: text

    readonly property color _fill: {
        if (!control.enabled)
            return variant === "ghost" ? "transparent" : Theme.surfaceSunken
        if (variant === "primary")
            return control.pressed ? Theme.accentPressed
                 : control.hovered ? Theme.accentHover : Theme.accent
        if (variant === "danger")
            return control.pressed ? Qt.darker(Theme.error, 1.2)
                 : control.hovered ? Qt.lighter(Theme.error, 1.08) : Theme.error
        if (variant === "ghost")
            return control.pressed ? Theme.surfaceSunken
                 : control.hovered ? Theme.accentSubtle : "transparent"
        return control.pressed ? Theme.surfaceSunken
             : control.hovered ? Theme.surfaceElevated : Theme.surface
    }

    readonly property color _label: {
        if (!control.enabled)
            return Theme.textDisabled
        if (variant === "primary" || variant === "danger")
            return Theme.textOnAccent
        if (variant === "ghost")
            return Theme.accent
        return Theme.textPrimary
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: control._fill
        border.width: control.variant === "secondary" ? Theme.borderWidth : 0
        border.color: control.enabled ? Theme.border : Theme.surfaceSunken

        Behavior on color {
            ColorAnimation {
                duration: control.pressed ? Theme.durationPress : Theme.durationHover
                easing.type: Theme.easing
            }
        }

        // A visible focus ring is what makes the whole interface usable from
        // the keyboard alone.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.focusRingWidth - 1
            radius: parent.radius + Theme.focusRingWidth + 1
            color: "transparent"
            border.width: Theme.focusRingWidth
            border.color: Theme.accent
            visible: control.visualFocus
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control._label
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
