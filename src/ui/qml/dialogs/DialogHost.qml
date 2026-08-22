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
        confirmDialog.heading     = options.heading || ""
        confirmDialog.subheading  = options.subheading || ""
        confirmDialog.body        = options.body || ""
        confirmDialog.confirmText = options.confirmText || qsTr("Continue")
        confirmDialog.cancelText  = options.cancelText || qsTr("Cancel")
        confirmDialog.destructive = options.destructive === true
        host._pending = onYes
        confirmDialog.open()
    }

    property var _pending: null

    ConfirmDialog {
        id: confirmDialog
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
