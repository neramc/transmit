import QtQuick
import QtQuick.Layouts
import Transmit.Theme

/// One labelled fact. Used for the machine description and archive summaries,
/// where two columns read better than sentences.
RowLayout {
    id: pair

    property string label: ""
    property string value: ""
    property int labelWidth: 150

    Layout.fillWidth: true
    spacing: Theme.spacingMd

    Accessible.role: Accessible.StaticText
    Accessible.name: pair.label
    Accessible.description: pair.value

    Text {
        text: pair.label
        Layout.preferredWidth: pair.labelWidth
        Layout.alignment: Qt.AlignTop
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
    }

    Text {
        text: pair.value
        Layout.fillWidth: true
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSmall
        elide: Text.ElideMiddle
    }
}
