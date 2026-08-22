import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Transmit.Components
import Transmit.Theme

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

            AppButton {
                id: cancelButton
                text: control.cancelText
                variant: "ghost"
                onClicked: control.reject()
            }

            AppButton {
                text: control.confirmText
                variant: control.destructive ? "danger" : "primary"
                onClicked: control.accept()
            }
        }
    }

    onOpened: cancelButton.forceActiveFocus()
}
