import QtQuick
import QtQuick.Controls.Basic
import Transmit.Theme

/// A scrolling area with the application's scrollbars.
///
/// It also sets the two things every scrolling page in Transmit wants and
/// which are easy to forget: content that is as wide as the view, and clipping
/// so rows do not paint outside it.
ScrollView {
    id: control

    contentWidth: availableWidth
    clip: true

    ScrollBar.vertical: AppScrollBar {
        parent: control
        x: control.mirrored ? 0 : control.width - width
        y: control.topPadding
        height: control.availableHeight
        policy: ScrollBar.AsNeeded
    }

    ScrollBar.horizontal: AppScrollBar {
        parent: control
        x: control.leftPadding
        y: control.height - height
        width: control.availableWidth
        policy: ScrollBar.AsNeeded
    }
}
