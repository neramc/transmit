import QtQuick
import QtQuick.Layouts
import Transmit.Components
import Transmit.Theme

/// The bar above the content: where you are, and the actions that belong to
/// this page. Actions are given by the shell rather than declared here, so the
/// bar stays a piece of chrome with no opinion about the application.
Item {
    id: bar

    property string title: ""
    property string subtitle: ""
    /// Items placed at the right-hand end. A plain alias rather than the
    /// default property: a default alias pointing at a descendant would also
    /// swallow this file's own children.
    property alias actions: actionRow.data

    implicitHeight: Sizing.toolbarHeight

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Spacing.xl
        anchors.rightMargin: Spacing.lg
        spacing: Spacing.md

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Text {
                Layout.fillWidth: true
                text: bar.title
                color: Colors.textPrimary
                font.family: Typography.family
                font.pixelSize: Typography.heading
                font.weight: Typography.semiBold
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                visible: bar.subtitle !== ""
                text: bar.subtitle
                color: Colors.textSecondary
                font.family: Typography.family
                font.pixelSize: Typography.caption
                elide: Text.ElideRight
            }
        }

        RowLayout {
            id: actionRow
            Layout.alignment: Qt.AlignVCenter
            spacing: Spacing.sm
        }
    }

    AppSeparator {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
