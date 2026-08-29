pragma Singleton

import QtQuick

/// Corner radii, from docs/design.md section 11.
///
/// Moderate on purpose. Heavily rounded corners read as a web page rather than
/// a desktop application, and section 11 rules out anything past 16 for
/// ordinary components. Pills are reserved for tags and status badges, where
/// the shape is carrying meaning rather than decoration.
QtObject {
    readonly property int chip:    4    ///< tags and small badges
    readonly property int control: 6    ///< buttons, inputs, combo boxes
    readonly property int card:    10
    readonly property int dialog:  14
    readonly property int pill:    999  ///< status badges only

    // Older names, mapped onto the scale above.
    readonly property int sm: chip
    readonly property int md: control
    readonly property int lg: card
}
