import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Transmit.Theme

/// Primary navigation. Kept to one level, because the application really only
/// does a handful of things and a nested tree would overstate its complexity.
Rectangle {
    id: sidebar

    property string currentPage: "home"
    property var entries: []

    signal navigate(string page)

    implicitWidth: Theme.sidebarWidth
    color: Theme.surfaceSunken

    Rectangle {
        width: Theme.borderWidth
        height: parent.height
        anchors.right: parent.right
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingXs

        Item { Layout.preferredHeight: Theme.spacingSm }

        Repeater {
            model: sidebar.entries

            Item {
                id: entry

                required property var modelData

                Layout.fillWidth: true
                Layout.preferredHeight: Theme.controlHeightLarge

                readonly property bool current: sidebar.currentPage === entry.modelData.page

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusMd
                    color: entry.current ? Theme.accentSubtle
                         : hover.hovered  ? Theme.surface
                                          : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationHover; easing.type: Theme.easing }
                    }
                }

                // The current item is marked with a bar as well as a tint, so
                // it is still obvious at low contrast settings.
                Rectangle {
                    width: 3
                    height: parent.height - Theme.spacingSm * 2
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    radius: Theme.radiusPill
                    color: Theme.accent
                    visible: entry.current
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingLg
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingSm
                    text: entry.modelData.label
                    elide: Text.ElideRight
                    color: entry.current ? Theme.accent : Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: entry.current ? Theme.weightSemiBold : Theme.weightRegular
                }

                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: sidebar.navigate(entry.modelData.page) }

                Accessible.role: Accessible.PageTab
                Accessible.name: entry.modelData.label
                Accessible.selected: entry.current
            }
        }

        Item { Layout.fillHeight: true }
    }
}
