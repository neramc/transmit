import QtQuick
import Transmit.Theme

/// A raised surface grouping related content. Elevation is expressed with a
/// border and a very restrained shadow rather than a heavy drop shadow.
Rectangle {
    id: card

    property bool interactive: false
    property bool selected: false
    property bool hovered: hoverHandler.hovered

    signal clicked()

    radius: Theme.radiusLg
    color: Theme.surface
    border.width: Theme.borderWidth
    border.color: selected ? Theme.accent
                : (interactive && hovered) ? Theme.borderStrong
                                           : Theme.border

    Behavior on border.color {
        ColorAnimation { duration: Theme.durationHover; easing.type: Theme.easing }
    }

    // A selected card gets a second inner ring so selection reads clearly
    // without relying on colour alone.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: parent.radius - 1
        color: "transparent"
        border.width: Theme.borderWidth
        border.color: Theme.accent
        visible: card.selected
        opacity: 0.45
    }

    HoverHandler {
        id: hoverHandler
        enabled: card.interactive
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: card.interactive
        onTapped: card.clicked()
    }
}
