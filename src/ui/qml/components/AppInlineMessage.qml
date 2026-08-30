import QtQuick
import QtQuick.Layouts
import Transmit.Theme

/// An explanation attached to where it applies, rather than a modal dialog the
/// user has to dismiss before they can act on it.
Rectangle {
    id: message

    /// "info", "success", "warning" or "error"
    property string tone: "info"
    property string title: ""
    property string body: ""

    /// Something to do about it, rather than only something to read. A message
    /// that says the drive holds an unfinished capture and leaves the user to
    /// work out where to go next has done half its job.
    property string actionText: ""
    property string secondaryActionText: ""

    signal actionTriggered
    signal secondaryActionTriggered

    Layout.fillWidth: true
    implicitHeight: layout.implicitHeight + Spacing.lg * 2
    radius: Radius.md

    color: tone === "success" ? Colors.successSubtle
         : tone === "warning" ? Colors.warningSubtle
         : tone === "error"   ? Colors.errorSubtle
                              : Colors.infoSubtle

    readonly property color accentColor: tone === "success" ? Colors.success
                                       : tone === "warning" ? Colors.warning
                                       : tone === "error"   ? Colors.error
                                                            : Colors.info

    Accessible.role: Accessible.StaticText
    Accessible.name: title
    Accessible.description: body

    Rectangle {
        width: 3
        height: parent.height - Spacing.md * 2
        anchors.left: parent.left
        anchors.leftMargin: Spacing.sm
        anchors.verticalCenter: parent.verticalCenter
        radius: Radius.pill
        color: message.accentColor
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Spacing.lg
        anchors.leftMargin: Spacing.xl
        spacing: Spacing.xs

        Text {
            text: message.title
            visible: message.title !== ""
            Layout.fillWidth: true
            color: Colors.textPrimary
            font.family: Typography.family
            font.pixelSize: Typography.body
            font.weight: Typography.semiBold
            wrapMode: Text.WordWrap
        }

        Text {
            text: message.body
            visible: message.body !== ""
            Layout.fillWidth: true
            color: Colors.textSecondary
            font.family: Typography.family
            font.pixelSize: Typography.small
            wrapMode: Text.WordWrap
            lineHeight: 1.3
        }

        RowLayout {
            visible: message.actionText !== "" || message.secondaryActionText !== ""
            Layout.topMargin: visible ? Spacing.xs : 0
            spacing: Spacing.sm

            AppButton {
                text: message.actionText
                visible: message.actionText !== ""
                variant: "primary"
                onClicked: message.actionTriggered()
            }

            AppButton {
                text: message.secondaryActionText
                visible: message.secondaryActionText !== ""
                variant: "ghost"
                onClicked: message.secondaryActionTriggered()
            }
        }
    }
}
