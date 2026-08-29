import QtQuick
import QtQuick.Controls.Basic
import Transmit.Theme

/// A tick box for a list.
///
/// `AppCheckRow` exists for the handful of choices whose consequences need a
/// paragraph next to them. This is for the other case: a row in a list of
/// seventy applications, where the whole control has to fit beside a name and
/// be recognisable at a glance.
CheckBox {
    id: control

    padding: 0
    spacing: label.text === "" ? 0 : Spacing.s8

    implicitHeight: Math.max(Sizing.minimumTouchTarget, label.implicitHeight)
    implicitWidth: box.width + control.spacing + label.implicitWidth

    Accessible.role: Accessible.CheckBox
    Accessible.name: text

    indicator: Rectangle {
        id: box

        // A square that stays square. Bound to the icon scale rather than a
        // number of its own so it grows with the rest of the interface.
        width: Sizing.iconSize
        height: Sizing.iconSize
        x: 0
        y: (control.height - height) / 2
        radius: Radius.chip

        color: control.checked || control.checkState === Qt.PartiallyChecked
               ? Colors.accent : Colors.surface
        opacity: control.enabled ? 1.0 : 0.5
        border.width: control.visualFocus ? Elevation.focusRingWidth : Elevation.borderWidth
        border.color: control.visualFocus ? Colors.accent
                    : control.checked || control.checkState === Qt.PartiallyChecked
                      ? Colors.accent
                    : control.hovered ? Colors.borderStrong
                                      : Colors.border

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration(Motion.hover)
                easing.type: Motion.easing
            }
        }

        // The tick and the dash are drawn rather than set as text so they keep
        // their weight next to the icon set instead of following whichever
        // font happens to have a check mark.
        Canvas {
            anchors.fill: parent
            visible: control.checked || control.checkState === Qt.PartiallyChecked

            Connections {
                target: control
                function onCheckStateChanged() { parent.requestPaint() }
            }

            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                const s = width / 18;
                ctx.scale(s, s);
                ctx.strokeStyle = Colors.textOnAccent;
                ctx.lineWidth = 2;
                ctx.lineCap = "round";
                ctx.lineJoin = "round";
                ctx.beginPath();
                if (control.checkState === Qt.PartiallyChecked) {
                    ctx.moveTo(4.5, 9); ctx.lineTo(13.5, 9);
                } else {
                    ctx.moveTo(4, 9.5); ctx.lineTo(7.5, 13); ctx.lineTo(14, 5.5);
                }
                ctx.stroke();
            }
        }
    }

    contentItem: Text {
        id: label

        leftPadding: box.width + control.spacing
        text: control.text
        visible: control.text !== ""
        verticalAlignment: Text.AlignVCenter
        color: control.enabled ? Colors.textPrimary : Colors.textDisabled
        font.family: Typography.family
        font.pixelSize: Typography.body
        elide: Text.ElideRight
    }
}
