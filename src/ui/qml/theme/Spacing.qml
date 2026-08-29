pragma Singleton

import QtQuick

/// The spacing scale from docs/design.md section 5. Every margin, padding and
/// gap in the interface is one of these, which is what keeps the rhythm
/// consistent without anyone having to think about it - and what lets
/// scripts/check-design-tokens.sh reject a number typed straight into a page.
///
/// Named by size rather than by role, because the same 16 is standard padding
/// in one place and a comfortable gap in another, and a name that claims
/// otherwise gets used for the wrong thing. The roles are the aliases below.
QtObject {
    readonly property int s4:  4    ///< micro: between an icon and its label
    readonly property int s8:  8    ///< tight: inside a compact control
    readonly property int s12: 12   ///< compact: between related controls
    readonly property int s16: 16   ///< standard: the default gap
    readonly property int s20: 20   ///< comfortable: inside a card
    readonly property int s24: 24   ///< section: between groups
    readonly property int s32: 32   ///< large: page insets
    readonly property int s40: 40   ///< major: between major sections
    readonly property int s48: 48   ///< page level
    readonly property int s64: 64   ///< exceptional: an empty state's breathing room

    // ------------------------------------------------------------- roles
    // Section 5 asks for 8/12/16/24/32 in preference to arbitrary values, so
    // every one of these resolves to one of those.
    readonly property int pagePadding:  s32   ///< the inset around a page's content
    readonly property int sectionGap:   s24   ///< between one section and the next
    readonly property int cardPadding:  s20   ///< inside a card
    readonly property int controlGap:   s12   ///< between two controls in a row
    readonly property int labelGap:     s8    ///< between a label and its control
    readonly property int inlineGap:    s4    ///< between an icon and its text

    // The old four-point names, kept so nothing breaks while the pages move
    // over. Anything still using them is a page that has not been revisited.
    readonly property int xs:   s4
    readonly property int sm:   s8
    readonly property int md:   s12
    readonly property int lg:   s16
    readonly property int xl:   s24
    readonly property int xxl:  s32
    readonly property int xxxl: s48
    readonly property int pageMargin: pagePadding
}
