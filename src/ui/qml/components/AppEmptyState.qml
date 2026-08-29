import QtQuick
import QtQuick.Layouts
import Transmit.Theme

/// Shown where a list would be.
///
/// docs/design.md section 22 asks an empty state to answer three questions:
/// what happened, why it is empty, and what to do next. The third is the one
/// that is usually missing, so `actionText` is here and the component looks
/// unfinished without it - which is the point.
ColumnLayout {
    id: empty

    property string title: ""
    property string body: ""

    /// The one thing to do next. Leave empty when there genuinely is nothing.
    property string actionText: ""
    property string glyph: ""

    signal actionTriggered()

    spacing: Spacing.s12

    AppIcon {
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: Spacing.s4
        visible: empty.glyph !== ""
        name: empty.glyph
        size: Sizing.iconSizeHero
        color: Colors.textDisabled
    }

    Text {
        text: empty.title
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        color: Colors.textPrimary
        font.family: Typography.family
        font.pixelSize: Typography.sectionTitle
        font.weight: Typography.medium
        wrapMode: Text.WordWrap
    }

    Text {
        text: empty.body
        visible: empty.body !== ""
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

    AppButton {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Spacing.s4
        visible: empty.actionText !== ""
        text: empty.actionText
        variant: "primary"
        onClicked: empty.actionTriggered()
    }
}
