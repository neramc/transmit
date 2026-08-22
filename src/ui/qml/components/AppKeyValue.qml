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

    /// A row whose value is empty says nothing and looks broken, so by default
    /// it is simply not there. Callers that want the gap can override this.
    visible: pair.value !== ""

    Layout.fillWidth: true
    spacing: Spacing.md

    Accessible.role: Accessible.StaticText
    Accessible.name: pair.label
    Accessible.description: pair.value

    Text {
        text: pair.label
        Layout.preferredWidth: pair.labelWidth
        Layout.alignment: Qt.AlignTop
        color: Colors.textSecondary
        font.family: Typography.family
        font.pixelSize: Typography.small
        wrapMode: Text.WordWrap
    }

    Text {
        text: pair.value
        Layout.fillWidth: true
        color: Colors.textPrimary
        font.family: Typography.family
        font.pixelSize: Typography.small
        elide: Text.ElideMiddle
    }
}
