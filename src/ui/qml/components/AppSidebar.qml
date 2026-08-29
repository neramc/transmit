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
///
/// It collapses to a column of icons (docs/design.md section 4). At 1280 wide
/// - the smallest size the application has to be useful at - a fixed 240 is a
/// fifth of the window given permanently to five words.
Rectangle {
    id: sidebar

    property string currentPage: "home"

    /// Icons only, with the label in a tooltip.
    property bool compact: false

    /// [{ page, label, icon }]
    property var entries: []

    signal navigate(string page)
    signal toggleRequested()

    implicitWidth: compact ? Sizing.sidebarWidthCompact : Sizing.sidebarWidth
    color: Colors.surfaceSunken

    Behavior on implicitWidth {
        NumberAnimation {
            duration: Motion.duration(Motion.panel)
            easing.type: Motion.easing
        }
    }

    Accessible.role: Accessible.PageTabList

    Rectangle {
        width: Elevation.borderWidth
        height: parent.height
        anchors.right: parent.right
        color: Colors.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Spacing.s12
        spacing: Spacing.s4

        // The application's name, so the window says what it is even when the
        // title bar is hidden by a tiling window manager. Compact keeps the
        // mark alone: it is the same shape as the icon in the dock.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Spacing.s8
            Layout.bottomMargin: Spacing.s12
            spacing: Spacing.s8

            AppIcon {
                name: "drive"
                size: Sizing.iconSizeLarge
                color: Colors.accentText
                Layout.leftMargin: sidebar.compact ? 0 : Spacing.s8
                Layout.alignment: sidebar.compact ? Qt.AlignHCenter : Qt.AlignLeft
            }

            Text {
                Layout.fillWidth: true
                visible: !sidebar.compact
                text: qsTr("Transmit")
                color: Colors.textPrimary
                font.family: Typography.family
                font.pixelSize: Typography.sectionTitle
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
                    radius: Radius.control
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
                        border.color: Colors.focusRing
                        visible: entry.activeFocus
                    }
                }

                // The current item is marked three ways - a tint, a bar and a
                // heavier label - because section 15 forbids saying it with
                // colour alone, and because two of the three survive being
                // printed, photographed or looked at by somebody who cannot
                // tell violet from grey.
                Rectangle {
                    width: Elevation.activeIndicatorWidth
                    height: parent.height - Spacing.s8 * 2
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    radius: Radius.pill
                    color: Colors.accent
                    visible: entry.current
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: sidebar.compact ? 0 : Spacing.s12
                    anchors.rightMargin: sidebar.compact ? 0 : Spacing.s8
                    spacing: Spacing.s8

                    AppIcon {
                        name: entry.modelData.icon !== undefined ? entry.modelData.icon : ""
                        size: Sizing.iconSizeMedium
                        color: entry.current ? Colors.accentText : Colors.textSecondary
                        Layout.alignment: sidebar.compact ? Qt.AlignCenter : Qt.AlignVCenter
                        Layout.fillWidth: sidebar.compact
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !sidebar.compact
                        text: entry.modelData.label
                        elide: Text.ElideRight
                        color: entry.current ? Colors.accentText : Colors.textPrimary
                        font.family: Typography.family
                        font.pixelSize: Typography.body
                        font.weight: entry.current ? Typography.semiBold : Typography.regular
                    }
                }

                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: sidebar.navigate(entry.modelData.page) }

                // Only when the label is not on screen. Section 27: a tooltip
                // on a button that already says what it does is noise.
                AppToolTip {
                    text: entry.modelData.label
                    visible: sidebar.compact && hover.hovered
                }

                Accessible.role: Accessible.PageTab
                Accessible.name: entry.modelData.label
                Accessible.selected: entry.current
                Accessible.onPressAction: sidebar.navigate(entry.modelData.page)
            }
        }

        Item { Layout.fillHeight: true }

        AppIconButton {
            Layout.alignment: sidebar.compact ? Qt.AlignHCenter : Qt.AlignLeft
            glyph: sidebar.compact ? "chevron-right" : "chevron-left"
            text: sidebar.compact ? qsTr("Expand the sidebar") : qsTr("Collapse the sidebar")
            tooltip: text + qsTr(" (Ctrl+B)")
            onClicked: sidebar.toggleRequested()
        }
    }
}
