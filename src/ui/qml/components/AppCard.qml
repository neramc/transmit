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

    radius: Radius.lg
    color: Colors.surface
    border.width: Elevation.borderWidth
    border.color: selected ? Colors.accent
                : (interactive && hovered) ? Colors.borderStrong
                                           : Colors.border

    Behavior on border.color {
        ColorAnimation {
            duration: Motion.duration(Motion.hover)
            easing.type: Motion.easing
        }
    }

    // A selected card gets a second inner ring so selection reads clearly
    // without relying on colour alone.
    Rectangle {
        anchors.fill: parent
        anchors.margins: Elevation.borderWidth
        radius: parent.radius - Elevation.borderWidth
        color: "transparent"
        border.width: Elevation.borderWidth
        border.color: Colors.accent
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
