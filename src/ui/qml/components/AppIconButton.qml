import QtQuick
import QtQuick.Controls.Basic
import Transmit.Theme

/// A square button whose label is a glyph.
///
/// It insists on an accessible name: an icon alone says nothing to a screen
/// reader, and a button nobody can name is a button nobody can use.
Button {
    id: control

    /// Not called `icon`: Button already has a FINAL `icon` group property, and
    /// overriding it is an error rather than a shadowing warning.
    property string glyph: ""
    property int iconSize: Sizing.iconSize
    property string tooltip: ""
    property color iconColor: control.enabled ? Colors.textSecondary : Colors.textDisabled

    implicitWidth: Sizing.controlHeight
    implicitHeight: Sizing.controlHeight
    padding: 0

    Accessible.role: Accessible.Button
    Accessible.name: text !== "" ? text : tooltip

    background: Rectangle {
        radius: Radius.md
        color: control.pressed ? Colors.surfaceSunken
             : control.hovered ? Colors.surfaceHover : "transparent"

        Behavior on color {
            ColorAnimation { duration: Motion.duration(Motion.hover); easing.type: Motion.easing }
        }

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

    contentItem: AppIcon {
        name: control.glyph
        size: control.iconSize
        color: control.iconColor
    }

    AppToolTip {
        text: control.tooltip
        visible: control.tooltip !== "" && control.hovered
    }
}
