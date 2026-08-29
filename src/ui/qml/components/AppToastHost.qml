import QtQuick
import QtQuick.Layouts
import Transmit.Theme

/// Brief feedback that does not stop the user doing something else.
///
/// docs/design.md section 20: short, says what happened, at most one action,
/// never blocking. And never for anything that has to be read carefully - a
/// toast that disappears is the wrong place for "these four files could not be
/// restored", which belongs in the report.
///
/// Toasts stack downward from the bottom right and time out. A toast with an
/// action stays until it is dismissed, because an action nobody had time to
/// press is worse than no action at all.
Item {
    id: host

    /// The toasts on screen, newest first. A plain array rather than a
    /// ListModel: a toast carries a function to run when its action is
    /// pressed, and ListModel cannot hold one - it warns about a null member
    /// and drops the role.
    property var entries: []

    property int nextId: 1

    function show(message, tone, actionText, action) {
        const entry = {
            key: host.nextId++,
            message: message,
            tone: tone === undefined ? "info" : tone,
            actionText: actionText === undefined ? "" : actionText,
            action: action === undefined ? null : action
        };
        host.entries = [entry].concat(host.entries).slice(0, host.maximumVisible);
    }

    function dismiss(key) {
        host.entries = host.entries.filter(entry => entry.key !== key);
    }

    readonly property int maximumVisible: 3

    /// How long a toast with nothing to press stays. Long enough to read a
    /// short sentence twice, which is what section 20 is asking for.
    readonly property int lifetimeMs: 5000

    anchors.fill: parent
    // The host covers the window so toasts can be positioned against it, but
    // nothing must be caught by it: everything underneath stays clickable.
    enabled: false

    ColumnLayout {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Spacing.s24
        width: Math.min(parent.width - Spacing.s24 * 2, Sizing.maxTextWidth / 2)
        spacing: Spacing.s8

        Repeater {
            model: host.entries

            Rectangle {
                id: toast

                required property var modelData

                readonly property int key: modelData.key
                readonly property string message: modelData.message
                readonly property string tone: modelData.tone
                readonly property string actionText: modelData.actionText
                readonly property var action: modelData.action

                Layout.fillWidth: true
                Layout.preferredHeight: toastRow.implicitHeight + Spacing.s12 * 2
                radius: Radius.control
                color: Colors.surfaceElevated
                border.width: Elevation.borderWidth
                border.color: toast.tone === "success" ? Colors.successBorder
                            : toast.tone === "warning" ? Colors.warningBorder
                            : toast.tone === "error"   ? Colors.errorBorder
                                                       : Colors.infoBorder
                enabled: true

                Accessible.role: Accessible.AlertMessage
                Accessible.name: toast.message

                // A stripe as well as a border colour, because section 9 does
                // not allow "it worked" and "it failed" to differ only in hue.
                Rectangle {
                    width: Elevation.activeIndicatorWidth
                    height: parent.height - Spacing.s8 * 2
                    anchors.left: parent.left
                    anchors.leftMargin: Spacing.s8
                    anchors.verticalCenter: parent.verticalCenter
                    radius: Radius.pill
                    color: toast.tone === "success" ? Colors.success
                         : toast.tone === "warning" ? Colors.warning
                         : toast.tone === "error"   ? Colors.error
                                                    : Colors.info
                }

                RowLayout {
                    id: toastRow
                    anchors.fill: parent
                    anchors.margins: Spacing.s12
                    anchors.leftMargin: Spacing.s24
                    spacing: Spacing.s8

                    AppIcon {
                        Layout.alignment: Qt.AlignVCenter
                        name: toast.tone === "success" ? "check"
                            : toast.tone === "error"   ? "alert"
                            : toast.tone === "warning" ? "alert" : "info"
                        size: Sizing.iconSizeSmall
                        color: toast.tone === "success" ? Colors.success
                             : toast.tone === "warning" ? Colors.warning
                             : toast.tone === "error"   ? Colors.error
                                                        : Colors.info
                    }

                    Text {
                        Layout.fillWidth: true
                        text: toast.message
                        color: Colors.textPrimary
                        font.family: Typography.family
                        font.pixelSize: Typography.secondary
                        wrapMode: Text.WordWrap
                    }

                    AppButton {
                        visible: toast.actionText !== ""
                        variant: "ghost"
                        text: toast.actionText
                        onClicked: {
                            if (toast.action)
                                toast.action();
                            host.dismiss(toast.key);
                        }
                    }

                    AppIconButton {
                        glyph: "close"
                        text: qsTr("Dismiss")
                        onClicked: host.dismiss(toast.key)
                    }
                }

                // A toast offering something to do waits to be answered.
                // Everything else gets out of the way on its own.
                Timer {
                    running: toast.actionText === ""
                    interval: host.lifetimeMs
                    onTriggered: host.dismiss(toast.key)
                }

                opacity: 0
                Component.onCompleted: opacity = 1
                Behavior on opacity {
                    NumberAnimation {
                        duration: Motion.duration(Motion.panel)
                        easing.type: Motion.easingEnter
                    }
                }
            }
        }
    }
}
