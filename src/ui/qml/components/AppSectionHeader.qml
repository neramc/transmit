import QtQuick
import QtQuick.Layouts
import Transmit.Theme

ColumnLayout {
    id: header

    property string title: ""
    property string subtitle: ""

    spacing: Theme.spacingXs

    Text {
        text: header.title
        Layout.fillWidth: true
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeTitle
        font.weight: Theme.weightSemiBold
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.Heading
        Accessible.name: header.title
    }

    Text {
        text: header.subtitle
        visible: header.subtitle !== ""
        Layout.fillWidth: true
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeBody
        wrapMode: Text.WordWrap
        lineHeight: 1.35
    }
}
