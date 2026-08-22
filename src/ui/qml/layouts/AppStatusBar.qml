import QtQuick
import QtQuick.Layouts
import Transmit.Components
import Transmit.Theme

/// A quiet line along the bottom saying whether anything is happening.
///
/// It exists so that long-running work is still visible after the user has
/// navigated away from the wizard that started it - otherwise a capture would
/// appear to have stopped the moment they looked at something else.
Rectangle {
    id: statusBar

    property bool busy: false
    property string message: ""
    property real progress: 0
    property string detail: ""

    implicitHeight: Sizing.statusBarHeight
    color: Colors.surfaceSunken

    AppSeparator {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Spacing.lg
        anchors.rightMargin: Spacing.lg
        spacing: Spacing.sm

        AppSpinner {
            size: 13
            running: statusBar.busy
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            text: statusBar.message
            color: statusBar.busy ? Colors.textPrimary : Colors.textSecondary
            font.family: Typography.family
            font.pixelSize: Typography.caption
            elide: Text.ElideRight
            Layout.maximumWidth: parent.width * 0.5
        }

        AppProgressBar {
            Layout.preferredWidth: 120
            Layout.alignment: Qt.AlignVCenter
            visible: statusBar.busy
            value: statusBar.progress
            unknownTotal: statusBar.progress <= 0
        }

        Text {
            Layout.fillWidth: true
            visible: statusBar.busy && statusBar.detail !== ""
            text: statusBar.detail
            color: Colors.textDisabled
            font.family: Typography.monoFamily
            font.pixelSize: Typography.caption
            elide: Text.ElideMiddle
        }

        Item { Layout.fillWidth: !(statusBar.busy && statusBar.detail !== "") }
    }

    Accessible.role: Accessible.StatusBar
    Accessible.name: message
}
