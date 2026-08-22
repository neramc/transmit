import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Transmit.Theme

/// Primary navigation. Kept to one level, because the application really only
/// does a handful of things and a nested tree would overstate its complexity.
///
/// It behaves as a tab list for the keyboard and for screen readers: arrow
/// keys move between destinations, and each entry reports whether it is the
/// one showing.
Rectangle {
    id: sidebar

    property string currentPage: "home"

    /// [{ page, label, icon }]
    property var entries: []

    signal navigate(string page)

    implicitWidth: Sizing.sidebarWidth
    color: Colors.surfaceSunken

    Accessible.role: Accessible.PageTabList

    Rectangle {
        width: Elevation.borderWidth
        height: parent.height
        anchors.right: parent.right
        color: Colors.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Spacing.md
        spacing: Spacing.xs

        // The application's name, so the window says what it is even when the
        // title bar is hidden by a tiling window manager.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Spacing.sm
            Layout.topMargin: Spacing.sm
            Layout.bottomMargin: Spacing.md
            spacing: Spacing.sm

            AppIcon {
                name: "drive"
                size: Sizing.iconSizeLarge
                color: Colors.accent
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Transmit")
                color: Colors.textPrimary
                font.family: Typography.family
                font.pixelSize: Typography.title
                font.weight: Typography.semiBold
                elide: Text.ElideRight
            }
        }

        Repeater {
            id: repeater
            model: sidebar.entries

            Item {
                id: entry

                required property var modelData
                required property int index

                Layout.fillWidth: true
                Layout.preferredHeight: Sizing.controlHeightLarge

                readonly property bool current: sidebar.currentPage === entry.modelData.page

                activeFocusOnTab: entry.current
                Keys.onUpPressed: sidebar.navigate(
                    sidebar.entries[(entry.index - 1 + sidebar.entries.length)
                                    % sidebar.entries.length].page)
                Keys.onDownPressed: sidebar.navigate(
                    sidebar.entries[(entry.index + 1) % sidebar.entries.length].page)
                Keys.onSpacePressed: sidebar.navigate(entry.modelData.page)
                Keys.onReturnPressed: sidebar.navigate(entry.modelData.page)

                Rectangle {
                    anchors.fill: parent
                    radius: Radius.md
                    color: entry.current ? Colors.accentSubtle
                         : hover.hovered  ? Colors.surface
                                          : "transparent"

                    Behavior on color {
                        ColorAnimation {
                            duration: Motion.duration(Motion.hover)
                            easing.type: Motion.easing
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: "transparent"
                        border.width: Elevation.focusRingWidth
                        border.color: Colors.accent
                        visible: entry.activeFocus
                    }
                }

                // The current item is marked with a bar as well as a tint, so
                // it is still obvious at low contrast settings.
                Rectangle {
                    width: 3
                    height: parent.height - Spacing.sm * 2
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    radius: Radius.pill
                    color: Colors.accent
                    visible: entry.current
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Spacing.md
                    anchors.rightMargin: Spacing.sm
                    spacing: Spacing.sm

                    AppIcon {
                        name: entry.modelData.icon !== undefined ? entry.modelData.icon : ""
                        size: Sizing.iconSize
                        color: entry.current ? Colors.accent : Colors.textSecondary
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Text {
                        Layout.fillWidth: true
                        text: entry.modelData.label
                        elide: Text.ElideRight
                        color: entry.current ? Colors.accent : Colors.textPrimary
                        font.family: Typography.family
                        font.pixelSize: Typography.body
                        font.weight: entry.current ? Typography.semiBold : Typography.regular
                    }
                }

                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: sidebar.navigate(entry.modelData.page) }

                Accessible.role: Accessible.PageTab
                Accessible.name: entry.modelData.label
                Accessible.selected: entry.current
                Accessible.onPressAction: sidebar.navigate(entry.modelData.page)
            }
        }

        Item { Layout.fillHeight: true }
    }
}
