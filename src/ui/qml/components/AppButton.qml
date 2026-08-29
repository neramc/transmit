import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Transmit.Theme

/// The application's button. Variants exist so intent is expressed once, at the
/// call site, instead of every caller restating colours.
///
/// It carries all six states docs/design.md section 13 requires: default,
/// hover, pressed, focused, disabled and loading. The last is the one that
/// used to be missing, and it is the one that matters most here - almost every
/// button in this application starts something that takes a while, and a
/// button that looks unchanged after being pressed gets pressed again.
Button {
    id: control

    /// "primary", "secondary", "ghost" or "danger"
    property string variant: "secondary"
    property bool destructive: variant === "danger"

    /// An icon name from AppIcon, shown before the label.
    property string glyph: ""

    /// Shows a spinner in place of the icon and refuses further presses. The
    /// label stays, so the button does not change width and the row does not
    /// jump.
    property bool loading: false

    enabled: !loading
    implicitHeight: Sizing.controlHeightLarge

    // Horizontal and vertical padding differ, and have to. A single 16 leaves
    // a 40-high button with 8 pixels of content area, which is less than a
    // line of text - the old contentItem was a bare Text and simply painted
    // outside its box, so nothing said so until the layout suite did.
    leftPadding: Spacing.s16
    rightPadding: Spacing.s16
    topPadding: Spacing.s8
    bottomPadding: Spacing.s8
    implicitWidth: Math.max(96, contentItem.implicitWidth + leftPadding + rightPadding)
    font.family: Typography.family
    font.pixelSize: Typography.body
    font.weight: variant === "primary" ? Typography.semiBold : Typography.medium

    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.description: loading ? qsTr("Working") : ""

    readonly property color _fill: {
        // Loading is not disabled: it is busy. Painting it grey would say the
        // button had been switched off, which is the opposite of what has
        // happened.
        if (control.loading)
            return variant === "primary" ? Colors.accent
                 : variant === "danger" ? Colors.error
                 : variant === "ghost" ? "transparent" : Colors.surface
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
        if (!control.enabled && !control.loading)
            return Colors.textDisabled
        if (variant === "primary" || variant === "danger")
            return Colors.textOnAccent
        if (variant === "ghost")
            return Colors.accentText
        return Colors.textPrimary
    }

    background: Rectangle {
        radius: Radius.control
        color: control._fill
        border.width: control.variant === "secondary" ? Elevation.borderWidth : 0
        border.color: control.enabled ? Colors.border : Colors.surfaceSunken

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration(control.pressed ? Motion.press : Motion.hover)
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
            border.color: Colors.focusRing
            visible: control.visualFocus
        }
    }

    contentItem: RowLayout {
        spacing: Spacing.s8

        // Left of centre only when there is something to show; otherwise the
        // label would sit off-centre by half a spinner.
        Item {
            Layout.preferredWidth: Sizing.iconSizeSmall
            Layout.preferredHeight: Sizing.iconSizeSmall
            Layout.alignment: Qt.AlignVCenter
            visible: control.loading || control.glyph !== ""

            AppIcon {
                anchors.fill: parent
                name: control.glyph
                size: Sizing.iconSizeSmall
                color: control._label
                visible: !control.loading && control.glyph !== ""
            }

            AppSpinner {
                anchors.centerIn: parent
                size: Sizing.iconSizeSmall
                color: control._label
                running: control.loading
                visible: control.loading
            }
        }

        Text {
            Layout.fillWidth: true
            text: control.text
            font: control.font
            color: control._label
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
