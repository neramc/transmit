import QtQuick
import QtQuick.Layouts
import Transmit.Theme

ColumnLayout {
    id: header

    property string title: ""
    property string subtitle: ""

    spacing: Spacing.xs

    Text {
        text: header.title
        Layout.fillWidth: true
        color: Colors.textPrimary
        font.family: Typography.family
        font.pixelSize: Typography.title
        font.weight: Typography.semiBold
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.Heading
        Accessible.name: header.title
    }

    Text {
        text: header.subtitle
        visible: header.subtitle !== ""
        Layout.fillWidth: true
        color: Colors.textSecondary
        font.family: Typography.family
        font.pixelSize: Typography.body
        wrapMode: Text.WordWrap
        lineHeight: 1.35
    }
}
