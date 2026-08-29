import QtQuick
import Transmit.Theme

/// Hosts the pages and cross-fades between them.
///
/// Two things matter here. Pages are built the first time they are shown
/// rather than all at once, so starting the application costs one page instead
/// of five; and once built they are kept, so a half-filled wizard is still
/// half-filled when the user comes back to it.
Item {
    id: view

    /// [{ page: "home", component: Component }]
    property var pages: []
    property string currentPage: ""

    /// The pages that have actually been built, oldest first. Exposed so that
    /// the laziness above can be asserted rather than assumed.
    property var loadedPages: []

    /// Section 32 asks for reduced density on a narrower window: at 1280 wide
    /// - the smallest the application has to be useful at - 32 on each side is
    /// five per cent of the window given to nothing at all.
    ///
    /// It lives here rather than in the theme because it needs a threshold
    /// from Sizing and a value from Spacing, and a singleton in the theme
    /// module cannot import that module - the engine rejects the pair as a
    /// cycle. One consumer, so nothing is duplicated by keeping it here.
    readonly property int pageInset: view.width < Sizing.smallWindow ? Spacing.s16
                                   : view.width < Sizing.mediumWindow ? Spacing.s24
                                                                      : Spacing.s32

    Repeater {
        model: view.pages

        delegate: Loader {
            id: slot

            required property var modelData

            readonly property bool current: view.currentPage === slot.modelData.page

            // Section 6: content stops growing past a readable width.
            readonly property int inset: view.pageInset

            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: inset
            anchors.bottomMargin: inset
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(view.width - inset * 2, Sizing.maxContentWidth)

            sourceComponent: slot.modelData.component
            asynchronous: true

            // Not a binding: once a page has been shown it stays loaded, and a
            // binding would tear it down again the moment the user left.
            active: false

            opacity: current ? 1 : 0
            visible: opacity > 0
            enabled: current
            z: current ? 1 : 0

            onCurrentChanged: if (current) active = true
            Component.onCompleted: if (current) active = true

            // Assigned rather than pushed to: mutating the array in place
            // would not notify anything bound to it.
            onActiveChanged: if (active)
                                 view.loadedPages = view.loadedPages.concat(
                                     [slot.modelData.page])

            Behavior on opacity {
                NumberAnimation {
                    duration: Motion.duration(Motion.panel)
                    easing.type: Motion.easing
                }
            }
        }
    }
}
