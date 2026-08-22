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

    Layout.fillWidth: true
    implicitHeight: layout.implicitHeight + Theme.spacingLg * 2
    radius: Theme.radiusMd

    color: tone === "success" ? Theme.successSubtle
         : tone === "warning" ? Theme.warningSubtle
         : tone === "error"   ? Theme.errorSubtle
                              : Theme.infoSubtle

    readonly property color accentColor: tone === "success" ? Theme.success
                                       : tone === "warning" ? Theme.warning
                                       : tone === "error"   ? Theme.error
                                                            : Theme.info

    Accessible.role: Accessible.StaticText
    Accessible.name: title
    Accessible.description: body

    Rectangle {
        width: 3
        height: parent.height - Theme.spacingMd * 2
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        radius: Theme.radiusPill
        color: message.accentColor
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        anchors.leftMargin: Theme.spacingXl
        spacing: Theme.spacingXs

        Text {
            text: message.title
            visible: message.title !== ""
            Layout.fillWidth: true
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBody
            font.weight: Theme.weightSemiBold
            wrapMode: Text.WordWrap
        }

        Text {
            text: message.body
            visible: message.body !== ""
            Layout.fillWidth: true
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
            lineHeight: 1.3
        }
    }
}
