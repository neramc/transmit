import QtQuick
import QtQuick.Layouts
import Transmit.Theme

/// A failure the user has to read, rather than a line of red text they will
/// scroll past.
///
/// docs/design.md section 23: never show a raw technical error as the primary
/// message. So `title` and `body` say what happened in words, `detail` holds
/// the machine's version behind a disclosure, and copying it is one press -
/// because the first thing anybody does with an error is try to send it to
/// somebody else, and retyping a path from a screenshot is how bug reports
/// become wrong.
ColumnLayout {
    id: state

    property string title: qsTr("Something went wrong")
    property string body: ""

    /// The underlying message, exactly as it arrived. Hidden until asked for.
    property string detail: ""

    property string actionText: qsTr("Try again")
    property bool actionEnabled: true

    signal retried()

    spacing: Spacing.s12

    AppIcon {
        Layout.alignment: Qt.AlignHCenter
        name: "alert"
        size: Sizing.iconSizeHero
        color: Colors.error
    }

    Text {
        text: state.title
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        color: Colors.textPrimary
        font.family: Typography.family
        font.pixelSize: Typography.sectionTitle
        font.weight: Typography.medium
        wrapMode: Text.WordWrap
    }

    Text {
        text: state.body
        visible: state.body !== ""
        Layout.fillWidth: true
        Layout.maximumWidth: Sizing.maxTextWidth
        Layout.alignment: Qt.AlignHCenter
        horizontalAlignment: Text.AlignHCenter
        color: Colors.textSecondary
        font.family: Typography.family
        font.pixelSize: Typography.body
        wrapMode: Text.WordWrap
        lineHeight: Typography.lineHeightNormal
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Spacing.s4
        spacing: Spacing.s8

        AppButton {
            visible: state.actionText !== ""
            enabled: state.actionEnabled
            text: state.actionText
            variant: "primary"
            onClicked: state.retried()
        }

        AppButton {
            visible: state.detail !== ""
            variant: "ghost"
            text: details.visible ? qsTr("Hide details") : qsTr("Details")
            onClicked: details.visible = !details.visible
        }

        AppButton {
            visible: state.detail !== ""
            variant: "ghost"
            text: qsTr("Copy error")
            onClicked: {
                clipboard.text = state.detail;
                clipboard.selectAll();
                clipboard.copy();
                clipboard.deselect();
            }
        }
    }

    // The only way to reach the clipboard from QML without a C++ helper is
    // through a TextEdit, so there is one, never shown.
    //
    // No width or height: a layout arranges its children, and an item that
    // also sets its own size is undefined behaviour rather than a smaller
    // item. Being invisible is enough - a layout leaves those out entirely.
    TextEdit {
        id: clipboard
        visible: false
    }

    Rectangle {
        id: details
        Layout.fillWidth: true
        Layout.maximumWidth: Sizing.maxTextWidth
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredHeight: detailText.implicitHeight + Spacing.s12 * 2
        visible: false
        radius: Radius.control
        color: Colors.surfaceSunken
        border.width: Elevation.borderWidth
        border.color: Colors.border

        Text {
            id: detailText
            anchors.fill: parent
            anchors.margins: Spacing.s12
            text: state.detail
            color: Colors.textSecondary
            font.family: Typography.monoFamily
            font.pixelSize: Typography.caption
            wrapMode: Text.Wrap
        }
    }
}
