import QtQuick
import QtQuick.Controls.Basic
import Transmit.Theme

/// A drop-down that matches the rest of the interface.
///
/// The Basic style deliberately ships unstyled controls, so a bare ComboBox
/// would have been the one place in the window that looked like it came from
/// somewhere else.
ComboBox {
    id: control

    implicitHeight: Sizing.controlHeightLarge
    implicitWidth: Math.max(180, implicitContentWidth + Spacing.xxl + Spacing.lg)
    leftPadding: Spacing.md
    rightPadding: Spacing.xxl
    font.family: Typography.family
    font.pixelSize: Typography.body

    Accessible.role: Accessible.ComboBox

    background: Rectangle {
        radius: Radius.md
        color: !control.enabled ? Colors.surfaceSunken
             : control.hovered  ? Colors.surfaceHover
                                : Colors.surface
        border.width: control.visualFocus || control.popup.visible
                      ? Elevation.focusRingWidth : Elevation.borderWidth
        border.color: control.visualFocus || control.popup.visible
                      ? Colors.accent : Colors.border

        Behavior on color {
            ColorAnimation { duration: Motion.duration(Motion.hover); easing.type: Motion.easing }
        }
    }

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Colors.textPrimary : Colors.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: AppIcon {
        x: control.width - width - Spacing.md
        y: control.topPadding + (control.availableHeight - height) / 2
        name: "chevron-down"
        size: 14
        color: control.enabled ? Colors.textSecondary : Colors.textDisabled
        rotation: control.popup.visible ? 180 : 0

        Behavior on rotation {
            NumberAnimation { duration: Motion.duration(Motion.hover); easing.type: Motion.easing }
        }
    }

    delegate: ItemDelegate {
        id: option

        required property int index
        required property var model

        width: ListView.view.width
        height: Sizing.controlHeight
        highlighted: control.highlightedIndex === option.index

        background: Rectangle {
            color: option.highlighted ? Colors.accentSubtle : "transparent"
            radius: Radius.sm
        }

        contentItem: Text {
            leftPadding: Spacing.sm
            text: control.textRole ? option.model[control.textRole] : option.model
            font.family: Typography.family
            font.pixelSize: Typography.body
            font.weight: control.currentIndex === option.index ? Typography.medium
                                                               : Typography.regular
            color: option.highlighted ? Colors.accent : Colors.textPrimary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    popup: Popup {
        y: control.height + Spacing.xs
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + Spacing.sm * 2, 260)
        padding: Spacing.xs

        background: Rectangle {
            radius: Radius.md
            color: Colors.surfaceElevated
            border.width: Elevation.borderWidth
            border.color: Colors.border
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}
        }

        enter: Transition {
            NumberAnimation {
                property: "opacity"; from: 0; to: 1
                duration: Motion.duration(Motion.hover); easing.type: Motion.easingEnter
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"; from: 1; to: 0
                duration: Motion.duration(Motion.press); easing.type: Motion.easingExit
            }
        }
    }
}
