pragma Singleton

import QtQuick

/// A four-point spacing scale. Every margin, padding and gap in the interface
/// is one of these, which is what keeps the rhythm consistent without anyone
/// having to think about it.
QtObject {
    readonly property int xs:  4
    readonly property int sm:  8
    readonly property int md:  12
    readonly property int lg:  16
    readonly property int xl:  24
    readonly property int xxl: 32
    readonly property int xxxl: 48

    /// Standard insets for the shell and for cards.
    readonly property int pageMargin: 32
    readonly property int cardPadding: 20
}
