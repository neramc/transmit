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

    implicitHeight: Sizing.controlHeightLarge
    implicitWidth: Math.max(96, contentItem.implicitWidth + Spacing.xl * 2)
    padding: Spacing.lg
    font.family: Typography.family
    font.pixelSize: Typography.body
    font.weight: variant === "primary" ? Typography.semiBold : Typography.medium

    Accessible.role: Accessible.Button
    Accessible.name: text

    readonly property color _fill: {
        if (!control.enabled)
            return variant === "ghost" ? "transparent" : Colors.surfaceSunken
        if (variant === "primary")
            return control.pressed ? Colors.accentPressed
                 : control.hovered ? Colors.accentHover : Colors.accent
        if (variant === "danger")
            return control.pressed ? Qt.darker(Colors.error, 1.2)
                 : control.hovered ? Qt.lighter(Colors.error, 1.08) : Colors.error
        if (variant === "ghost")
            return control.pressed ? Colors.surfaceSunken
                 : control.hovered ? Colors.accentSubtle : "transparent"
        return control.pressed ? Colors.surfaceSunken
             : control.hovered ? Colors.surfaceElevated : Colors.surface
    }

    readonly property color _label: {
        if (!control.enabled)
            return Colors.textDisabled
        if (variant === "primary" || variant === "danger")
            return Colors.textOnAccent
        if (variant === "ghost")
            return Colors.accent
        return Colors.textPrimary
    }

    background: Rectangle {
        radius: Radius.md
        color: control._fill
        border.width: control.variant === "secondary" ? Elevation.borderWidth : 0
        border.color: control.enabled ? Colors.border : Colors.surfaceSunken

        Behavior on color {
            ColorAnimation {
                duration: control.pressed ? Motion.press : Motion.hover
                easing.type: Motion.easing
            }
        }

        // A visible focus ring is what makes the whole interface usable from
        // the keyboard alone.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -Elevation.focusRingWidth - 1
            radius: parent.radius + Elevation.focusRingWidth + 1
            color: "transparent"
            border.width: Elevation.focusRingWidth
            border.color: Colors.accent
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
