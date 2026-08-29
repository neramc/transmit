import QtQuick
import QtQuick.Controls.Basic
import Transmit.Theme

/// A field for narrowing a long list.
///
/// Separate from `AppTextField` because it carries two affordances that only
/// make sense here: the glyph that says what the field is for without a label
/// above it, and a clear button, so getting back to the whole list does not
/// mean selecting the text and deleting it.
TextField {
    id: control

    implicitHeight: Sizing.controlHeight
    leftPadding: glyph.x + glyph.width + Spacing.s8
    rightPadding: clear.visible ? Spacing.s32 : Spacing.s12
    font.family: Typography.family
    font.pixelSize: Typography.body
    color: Colors.textPrimary
    placeholderTextColor: Colors.textDisabled
    selectionColor: Colors.accent
    selectedTextColor: Colors.textOnAccent
    placeholderText: qsTr("Search")

    Accessible.role: Accessible.EditableText
    Accessible.name: placeholderText

    // Escape empties the field rather than leaving the page, which is what
    // somebody who has just typed into it means by it.
    Keys.onEscapePressed: function(event) {
        if (control.text !== "") {
            control.text = "";
            event.accepted = true;
        }
    }

    background: Rectangle {
        radius: Radius.control
        color: control.enabled ? Colors.surface : Colors.surfaceSunken
        border.width: control.activeFocus ? Elevation.focusRingWidth : Elevation.borderWidth
        border.color: control.activeFocus ? Colors.accent
                    : control.hovered ? Colors.borderStrong
                                      : Colors.border

        Behavior on border.color {
            ColorAnimation {
                duration: Motion.duration(Motion.hover)
                easing.type: Motion.easing
            }
        }
    }

    AppIcon {
        id: glyph
        name: "search"
        size: Sizing.iconSizeSmall
        color: Colors.textSecondary
        x: Spacing.s12
        anchors.verticalCenter: parent.verticalCenter
    }

    AppIconButton {
        id: clear
        glyph: "close"
        visible: control.text !== ""
        iconSize: Sizing.iconSizeSmall
        implicitWidth: Sizing.minimumTouchTarget
        implicitHeight: Sizing.minimumTouchTarget
        anchors.right: parent.right
        anchors.rightMargin: Spacing.s8
        anchors.verticalCenter: parent.verticalCenter
        tooltip: qsTr("Clear the search")
        onClicked: control.text = ""
    }
}
