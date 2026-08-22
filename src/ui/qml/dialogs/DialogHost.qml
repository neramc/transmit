import QtQuick
import Transmit.Theme

/// One place dialogs are opened from.
///
/// Pages ask for a confirmation instead of each carrying its own Dialog
/// instance, which keeps the modals out of the page tree and means a page that
/// is unloaded cannot leave a dialog stranded on screen.
Item {
    id: host

    /// Asks the question, and calls `onYes` only if the user says yes.
    function confirm(options, onYes) {
        // Built on the first question rather than at startup: most sessions
        // never ask one, and a dialog nobody has opened is a Popup, a
        // background, a header and two buttons built for nothing.
        if (loader.status !== Loader.Ready)
            loader.active = true

        const dialog = loader.item
        dialog.heading     = options.heading || ""
        dialog.subheading  = options.subheading || ""
        dialog.body        = options.body || ""
        dialog.confirmText = options.confirmText || qsTr("Continue")
        dialog.cancelText  = options.cancelText || qsTr("Cancel")
        dialog.destructive = options.destructive === true
        host._pending = onYes
        dialog.open()
    }

    property var _pending: null

    Loader {
        id: loader
        active: false

        sourceComponent: ConfirmDialog {
            objectName: "confirmDialog"

            onAccepted: {
                // Cleared first: the callback may open another dialog, and the
                // pending one has already had its answer.
                const callback = host._pending
                host._pending = null
                if (callback)
                    callback()
            }
            onRejected: host._pending = null
        }
    }
}
