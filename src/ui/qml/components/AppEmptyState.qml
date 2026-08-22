import QtQuick
import QtQuick.Layouts
import Transmit.Theme

/// Shown where a list would be. An empty state should say what is missing and
/// what to do about it, not just that there is nothing here.
ColumnLayout {
    id: empty

    property string title: ""
    property string body: ""

    spacing: Spacing.sm

    Text {
        text: empty.title
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        color: Colors.textPrimary
        font.family: Typography.family
        font.pixelSize: Typography.heading
        font.weight: Typography.medium
        wrapMode: Text.WordWrap
    }

    Text {
        text: empty.body
        visible: empty.body !== ""
        Layout.fillWidth: true
        Layout.maximumWidth: 420
        Layout.alignment: Qt.AlignHCenter
        horizontalAlignment: Text.AlignHCenter
        color: Colors.textSecondary
        font.family: Typography.family
        font.pixelSize: Typography.body
        wrapMode: Text.WordWrap
        lineHeight: 1.35
    }
}
