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

    spacing: Spacing.xs

    Text {
        text: field.label
        visible: field.label !== ""
        color: Colors.textSecondary
        font.family: Typography.family
        font.pixelSize: Typography.small
        font.weight: Typography.medium
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
        color: field.error ? Colors.error : Colors.textSecondary
        font.family: Typography.family
        font.pixelSize: Typography.caption
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}
