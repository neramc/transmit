import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Transmit.Components
import Transmit.Theme
import Transmit.ThemeState

/// A yes-or-no question about something the user cannot easily take back.
///
/// The buttons say what they do rather than "OK" and "Cancel", because a
/// dialog whose buttons are generic makes the user re-read the question to
/// work out which one is which.
AppDialog {
    id: control

    property string body: ""
    property string confirmText: qsTr("Continue")
    property string cancelText: qsTr("Cancel")

    /// Colours the confirming button as a warning and leaves the focus on the
    /// safe side, so the destructive answer is never the one Return picks.
    property bool destructive: false

    // Dialog's own accepted/rejected rather than a signal of our own: the
    // buttons then go through accept() and reject(), which is what a keyboard,
    // a window manager's close button and a test all reach for.

    contentItem: ColumnLayout {
        spacing: Spacing.lg

        Accessible.role: Accessible.Dialog
        Accessible.name: control.heading
        Accessible.description: control.body

        Text {
            Layout.fillWidth: true
            visible: control.body !== ""
            text: control.body
            color: Colors.textPrimary
            font.family: Typography.family
            font.pixelSize: Typography.body
            lineHeight: Typography.lineHeightNormal
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Spacing.xs
            spacing: Spacing.md

            Item { Layout.fillWidth: true }

            // Which of the two comes first is not a matter of taste. Windows
            // and Plasma put the button that goes ahead on the left of the
            // pair; macOS and GNOME put it on the right. Somebody who has
            // dismissed ten thousand dialogs is not reading these - they are
            // moving the pointer to where the button has always been - so
            // getting the order wrong is how a person cancels something they
            // meant to confirm.
            //
            // The two are given their role rather than reordered, because a
            // RowLayout lays its children out in the order they are written
            // and there is no property that changes that without mirroring
            // the whole row, spacer and all.
            component DialogButton: AppButton {
                required property bool isConfirm
                text: isConfirm ? control.confirmText : control.cancelText
                variant: isConfirm ? (control.destructive ? "danger" : "primary") : "ghost"
                onClicked: isConfirm ? control.accept() : control.reject()
            }

            DialogButton {
                id: firstButton
                isConfirm: ThemeState.acceptFirst
            }

            DialogButton {
                id: secondButton
                isConfirm: !ThemeState.acceptFirst
            }
        }
    }

    // The one that does nothing takes the focus, so a stray Return dismisses
    // rather than confirms - wherever that button happens to be.
    onOpened: (ThemeState.acceptFirst ? secondButton : firstButton).forceActiveFocus()
}
