import QtQuick
import Transmit.Theme

/// A busy indicator for work whose length is not yet known.
///
/// The ring is drawn once and then spun by the scene graph, so an indicator
/// left running costs a transform per frame rather than a repaint.
Item {
    id: spinner

    property int size: Sizing.iconSize
    property color color: Colors.accent
    property bool running: true

    implicitWidth: size
    implicitHeight: size
    visible: running

    Canvas {
        id: ring
        anchors.fill: parent
        antialiasing: true
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const r = Math.min(width, height) / 2 - 1.5;
            ctx.lineWidth = 2;
            ctx.lineCap = "round";

            ctx.strokeStyle = Qt.rgba(spinner.color.r, spinner.color.g, spinner.color.b, 0.2);
            ctx.beginPath();
            ctx.arc(width / 2, height / 2, r, 0, Math.PI * 2);
            ctx.stroke();

            // A three-quarter gap is what reads as motion once it turns.
            ctx.strokeStyle = spinner.color;
            ctx.beginPath();
            ctx.arc(width / 2, height / 2, r, -Math.PI / 2, Math.PI * 0.15);
            ctx.stroke();
        }

        Connections {
            target: spinner
            function onColorChanged() { ring.requestPaint() }
        }

        RotationAnimator on rotation {
            running: spinner.running && spinner.visible && !Motion.reduced
            loops: Animation.Infinite
            from: 0
            to: 360
            duration: Motion.loop
        }
    }
}
