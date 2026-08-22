pragma Singleton

import QtQuick

/// Corner radii. Kept small deliberately: heavily rounded corners read as a
/// web page rather than a desktop application.
QtObject {
    readonly property int sm: 4
    readonly property int md: 6
    readonly property int lg: 10
    readonly property int pill: 999
}
