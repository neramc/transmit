import QtQuick
// Overlay - the dimmed backdrop and the item a modal centres itself on -
// lives in QtQuick.Controls rather than in a style, so this file imports the
// module itself. The style is still Basic: main.cpp sets it before the engine
// loads anything.
import QtQuick.Controls
import QtQuick.Layouts
import Transmit.Components
import Transmit.Theme

/// The base modal.
///
/// Everything a dialog needs to get right - the dimmed backdrop, closing on
/// Escape, focus staying inside it, a title that screen readers announce -
/// lives here so that individual dialogs are only their content.
Dialog {
    id: control

    /// Shown above the body. A dialog without one is a dialog the user has to
    /// read twice to work out what it is asking.
    property string heading: ""
    property string subheading: ""

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - Spacing.xxl * 2 : 480, 520)
    padding: Spacing.xl
    closePolicy: Popup.CloseOnEscape

    // Accessible attaches to Items, and a Popup is not one - the role goes on
    // the content instead, which is the item a screen reader actually walks.
    background: Rectangle {
        radius: Radius.lg
        color: Colors.surfaceElevated
        border.width: Elevation.borderWidth
        border.color: Colors.border
    }

    Overlay.modal: Rectangle {
        color: Colors.overlay

        Behavior on opacity {
            NumberAnimation { duration: Motion.duration(Motion.dialog) }
        }
    }

    header: ColumnLayout {
        visible: control.heading !== ""
        spacing: Spacing.xs

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Spacing.xl
            Layout.rightMargin: Spacing.xl
            Layout.topMargin: Spacing.xl
            text: control.heading
            color: Colors.textPrimary
            font.family: Typography.family
            font.pixelSize: Typography.heading
            font.weight: Typography.semiBold
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Spacing.xl
            Layout.rightMargin: Spacing.xl
            visible: control.subheading !== ""
            text: control.subheading
            color: Colors.textSecondary
            font.family: Typography.family
            font.pixelSize: Typography.small
            lineHeight: Typography.lineHeightNormal
            wrapMode: Text.WordWrap
        }
    }

    // A dialog that grows out of nothing draws the eye to itself, which is the
    // whole point of interrupting the user.
    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"; from: 0; to: 1
                duration: Motion.duration(Motion.dialog); easing.type: Motion.easingEnter
            }
            NumberAnimation {
                property: "scale"; from: 0.96; to: 1
                duration: Motion.duration(Motion.dialog); easing.type: Motion.easingEnter
            }
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"; from: 1; to: 0
            duration: Motion.duration(Motion.press); easing.type: Motion.easingExit
        }
    }
}
