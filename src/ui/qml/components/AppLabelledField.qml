import QtQuick
import QtQuick.Layouts
import Transmit.Theme

/// Label, control and helper text as one unit, so forms stay aligned and every
/// control carries an accessible name.
ColumnLayout {
    id: field

    property string label: ""
    property string helperText: ""
    property bool error: false
    default property alias content: holder.data

    spacing: Theme.spacingXs

    Text {
        text: field.label
        visible: field.label !== ""
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSmall
        font.weight: Theme.weightMedium
    }

    Item {
        id: holder
        Layout.fillWidth: true
        implicitHeight: childrenRect.height

        onChildrenChanged: {
            for (let i = 0; i < children.length; ++i) {
                children[i].anchors.left = holder.left
                children[i].anchors.right = holder.right
            }
        }
    }

    Text {
        text: field.helperText
        visible: field.helperText !== ""
        color: field.error ? Theme.error : Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCaption
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}
