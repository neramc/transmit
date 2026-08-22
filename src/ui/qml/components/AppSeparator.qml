import QtQuick
import Transmit.Theme

/// A hairline rule. It carries an orientation so callers do not have to
/// remember which of width and height to pin.
Rectangle {
    id: separator

    /// Qt.Horizontal or Qt.Vertical.
    property int orientation: Qt.Horizontal

    implicitWidth: orientation === Qt.Horizontal ? 0 : Elevation.borderWidth
    implicitHeight: orientation === Qt.Horizontal ? Elevation.borderWidth : 0
    color: Colors.border

    Accessible.role: Accessible.Separator
}
