import QtQuick
import Transmit.Theme

/// The icon set, drawn rather than shipped.
///
/// A handful of line icons is not worth an image dependency: drawing them on a
/// 24-unit grid means they stay crisp at any scale factor, take the theme's
/// colour directly, and add nothing to the binary. Every glyph is a stroked
/// outline at the same weight so they sit together as a set.
Canvas {
    id: icon

    /// One of the names handled in onPaint. An unknown name draws nothing,
    /// which is the right failure: a missing icon should not be a broken one.
    property string name: ""
    property int size: Sizing.iconSize
    property color color: Colors.textPrimary

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    antialiasing: true
    renderStrategy: Canvas.Cooperative

    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onSizeChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d");
        ctx.reset();
        if (name === "")
            return;

        // Everything below is authored on a 24x24 grid and scaled once here,
        // so the shapes can be read as coordinates rather than fractions.
        const s = width / 24;
        ctx.scale(s, s);
        ctx.strokeStyle = color;
        ctx.fillStyle = color;
        ctx.lineWidth = 1.75;
        ctx.lineCap = "round";
        ctx.lineJoin = "round";

        const line = (x1, y1, x2, y2) => {
            ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();
        };
        const poly = (pts, close) => {
            ctx.beginPath();
            ctx.moveTo(pts[0], pts[1]);
            for (let i = 2; i < pts.length; i += 2)
                ctx.lineTo(pts[i], pts[i + 1]);
            if (close)
                ctx.closePath();
            ctx.stroke();
        };
        const circle = (cx, cy, r, fill) => {
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, Math.PI * 2);
            if (fill) ctx.fill(); else ctx.stroke();
        };
        const box = (x, y, w, h, r) => {
            ctx.beginPath();
            ctx.moveTo(x + r, y);
            ctx.arcTo(x + w, y,     x + w, y + h, r);
            ctx.arcTo(x + w, y + h, x,     y + h, r);
            ctx.arcTo(x,     y + h, x,     y,     r);
            ctx.arcTo(x,     y,     x + w, y,     r);
            ctx.closePath();
            ctx.stroke();
        };

        switch (name) {
        case "home":
            poly([3, 10, 12, 3, 21, 10, 21, 20, 3, 20], true);
            poly([9.5, 20, 9.5, 14, 14.5, 14, 14.5, 20], false);
            break;

        // Out of this computer, onto the drive.
        case "upload":
            poly([4, 15, 4, 20, 20, 20, 20, 15], false);
            line(12, 3.5, 12, 14.5);
            poly([7.5, 8, 12, 3.5, 16.5, 8], false);
            break;

        // Off the drive, onto this computer.
        case "download":
            poly([4, 15, 4, 20, 20, 20, 20, 15], false);
            line(12, 3.5, 12, 14.5);
            poly([7.5, 10, 12, 14.5, 16.5, 10], false);
            break;

        case "list":
            circle(4.75, 7, 1.1, true);
            circle(4.75, 12, 1.1, true);
            circle(4.75, 17, 1.1, true);
            line(9, 7, 20, 7);
            line(9, 12, 20, 12);
            line(9, 17, 20, 17);
            break;

        case "sliders":
            line(4, 8, 20, 8);
            line(4, 16, 20, 16);
            circle(9, 8, 2.4, false);
            circle(15, 16, 2.4, false);
            break;

        case "drive":
            box(3, 6, 18, 12, 2);
            line(7, 6, 7, 18);
            circle(16.5, 12, 1.15, true);
            break;

        case "lock":
            box(4.5, 10.5, 15, 9.5, 2);
            line(8, 10.5, 8, 7.5);
            ctx.beginPath();
            ctx.arc(12, 7.5, 4, Math.PI, 0);
            ctx.stroke();
            line(16, 7.5, 16, 10.5);
            break;

        case "shield":
            poly([12, 3, 20, 6, 20, 12, 12, 21, 4, 12, 4, 6], true);
            poly([8.5, 12, 11, 14.5, 15.5, 9.5], false);
            break;

        case "check":
            poly([5, 12.5, 10, 17.5, 19, 6.5], false);
            break;

        case "alert":
            poly([12, 4, 21.5, 20, 2.5, 20], true);
            line(12, 10, 12, 14.5);
            circle(12, 17.4, 1.1, true);
            break;

        case "info":
            circle(12, 12, 8.75, false);
            line(12, 11, 12, 16.5);
            circle(12, 7.75, 1.1, true);
            break;

        case "close":
            line(6, 6, 18, 18);
            line(18, 6, 6, 18);
            break;

        case "chevron-down":  poly([6, 9.5, 12, 15.5, 18, 9.5], false); break;
        case "chevron-up":    poly([6, 14.5, 12, 8.5, 18, 14.5], false); break;
        case "chevron-right": poly([9.5, 5, 15.5, 12, 9.5, 19], false); break;
        case "chevron-left":  poly([14.5, 5, 8.5, 12, 14.5, 19], false); break;

        case "refresh":
            ctx.beginPath();
            ctx.arc(12, 12, 8, Math.PI * 0.35, Math.PI * 1.75);
            ctx.stroke();
            poly([16.5, 3.5, 17.6, 8.4, 12.7, 8.2], false);
            break;

        case "sun":
            circle(12, 12, 4.25, false);
            for (let i = 0; i < 8; ++i) {
                const a = i * Math.PI / 4;
                line(12 + Math.cos(a) * 7, 12 + Math.sin(a) * 7,
                     12 + Math.cos(a) * 9.3, 12 + Math.sin(a) * 9.3);
            }
            break;

        case "moon":
            // Two arcs rather than a filled crescent, so it keeps the same
            // stroke weight as the rest of the set.
            ctx.beginPath();
            ctx.arc(12, 12, 8.5, Math.PI * 0.42, Math.PI * 1.62);
            ctx.arc(8.4, 12, 8.5, Math.PI * 1.72, Math.PI * 0.32, true);
            ctx.stroke();
            break;

        case "folder":
            poly([3, 19, 3, 5.5, 9.5, 5.5, 11.5, 8.5, 21, 8.5, 21, 19], true);
            break;

        case "search":
            circle(10.5, 10.5, 6.5, false);
            line(15.2, 15.2, 20, 20);
            break;

        // A grid of tiles: the list of installed programs.
        case "apps":
            box(3.5, 3.5, 7, 7, 1.5);
            box(13.5, 3.5, 7, 7, 1.5);
            box(3.5, 13.5, 7, 7, 1.5);
            box(13.5, 13.5, 7, 7, 1.5);
            break;

        default:
            break;
        }
    }
}
