import QtQuick
import Polymath

// PmIcon — crisp stroke icons drawn on a Canvas.  No icon font is involved, so
// glyphs render identically everywhere — including the offscreen software
// renderer used by capture_views, which has no emoji/symbol font fallback.
//
// Icons are authored on a 24×24 design grid and scaled to the item's size.
// Add new ones as a case in paint(); keep strokes simple (the whole set is
// line-art with round caps so it reads cleanly at 16–20 px).
Canvas {
    id: icon
    property string name: ""
    property color  color: Style.textDim
    property real   stroke: 1.7

    implicitWidth: 18
    implicitHeight: 18
    antialiasing: true
    renderStrategy: Canvas.Cooperative

    onNameChanged:  requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        if (name.length === 0) return

        const s = Math.min(width, height) / 24
        ctx.translate((width - 24 * s) / 2, (height - 24 * s) / 2)
        ctx.scale(s, s)
        ctx.strokeStyle = String(icon.color)
        ctx.fillStyle   = String(icon.color)
        ctx.lineWidth   = stroke / s
        ctx.lineCap  = "round"
        ctx.lineJoin = "round"

        // tiny helpers, all in 24-grid coordinates
        function line(x1, y1, x2, y2) { ctx.moveTo(x1, y1); ctx.lineTo(x2, y2) }
        function circle(cx, cy, r)    { ctx.moveTo(cx + r, cy); ctx.arc(cx, cy, r, 0, Math.PI * 2) }

        ctx.beginPath()
        switch (name) {
        case "home":
            ctx.moveTo(4, 11); ctx.lineTo(12, 4.5); ctx.lineTo(20, 11)
            ctx.moveTo(6, 9.5); ctx.lineTo(6, 19.5); ctx.lineTo(18, 19.5); ctx.lineTo(18, 9.5)
            ctx.moveTo(10, 19.5); ctx.lineTo(10, 14); ctx.lineTo(14, 14); ctx.lineTo(14, 19.5)
            break
        case "chat":
            ctx.moveTo(5, 5); ctx.lineTo(19, 5); ctx.lineTo(19, 15.5); ctx.lineTo(11, 15.5)
            ctx.lineTo(7, 19.5); ctx.lineTo(7, 15.5); ctx.lineTo(5, 15.5); ctx.closePath()
            break
        case "camera":
            ctx.moveTo(3.5, 7.5); ctx.lineTo(14.5, 7.5); ctx.lineTo(14.5, 16.5)
            ctx.lineTo(3.5, 16.5); ctx.closePath()
            ctx.moveTo(14.5, 11); ctx.lineTo(20.5, 7.5); ctx.lineTo(20.5, 16.5); ctx.lineTo(14.5, 13)
            break
        case "tasks":
            line(5, 6.5, 7, 8.5); ctx.moveTo(7, 8.5); ctx.lineTo(9.5, 5.5)
            line(12.5, 7, 19.5, 7)
            line(5, 13.5, 7, 15.5); ctx.moveTo(7, 15.5); ctx.lineTo(9.5, 12.5)
            line(12.5, 14, 19.5, 14)
            line(12.5, 20, 19.5, 20)
            line(5.5, 20, 9, 20)
            break
        case "clock":
            circle(12, 12, 8)
            ctx.moveTo(12, 7.5); ctx.lineTo(12, 12); ctx.lineTo(15.5, 14)
            break
        case "cart":
            ctx.moveTo(4, 5.5); ctx.lineTo(6.5, 5.5); ctx.lineTo(8.6, 15.5); ctx.lineTo(17.5, 15.5)
            ctx.lineTo(19.8, 8); ctx.lineTo(7.2, 8)
            circle(9.7, 19.2, 1.4)
            circle(16.6, 19.2, 1.4)
            break
        case "person":
            circle(12, 8.6, 3.6)
            ctx.moveTo(5.4, 19.8)
            ctx.bezierCurveTo(6.6, 15.4, 17.4, 15.4, 18.6, 19.8)
            break
        case "chip":
            ctx.moveTo(7, 7); ctx.lineTo(17, 7); ctx.lineTo(17, 17); ctx.lineTo(7, 17); ctx.closePath()
            ctx.moveTo(10.4, 10.4); ctx.lineTo(13.6, 10.4); ctx.lineTo(13.6, 13.6); ctx.lineTo(10.4, 13.6); ctx.closePath()
            line(10, 7, 10, 4); line(14, 7, 14, 4)
            line(10, 20, 10, 17); line(14, 20, 14, 17)
            line(4, 10, 7, 10); line(4, 14, 7, 14)
            line(17, 10, 20, 10); line(17, 14, 20, 14)
            break
        case "shield":
            ctx.moveTo(12, 3.8); ctx.lineTo(19, 6.4); ctx.lineTo(19, 11.6)
            ctx.bezierCurveTo(19, 16.4, 16.2, 19.2, 12, 20.8)
            ctx.bezierCurveTo(7.8, 19.2, 5, 16.4, 5, 11.6)
            ctx.lineTo(5, 6.4); ctx.closePath()
            ctx.moveTo(9, 11.8); ctx.lineTo(11.2, 14); ctx.lineTo(15.2, 9.6)
            break
        case "phone":
            ctx.moveTo(9.5, 3.5); ctx.lineTo(14.5, 3.5)
            ctx.bezierCurveTo(15.6, 3.5, 16.5, 4.4, 16.5, 5.5)
            ctx.lineTo(16.5, 18.5)
            ctx.bezierCurveTo(16.5, 19.6, 15.6, 20.5, 14.5, 20.5)
            ctx.lineTo(9.5, 20.5)
            ctx.bezierCurveTo(8.4, 20.5, 7.5, 19.6, 7.5, 18.5)
            ctx.lineTo(7.5, 5.5)
            ctx.bezierCurveTo(7.5, 4.4, 8.4, 3.5, 9.5, 3.5)
            line(11, 17.4, 13, 17.4)
            break
        case "settings":   // "tune" sliders — reads as settings without gear teeth
            line(4, 7, 12.6, 7);    circle(15.2, 7, 2.3);  line(17.8, 7, 20, 7)
            line(4, 12, 5.6, 12);   circle(8.2, 12, 2.3);  line(10.8, 12, 20, 12)
            line(4, 17, 9.6, 17);   circle(12.2, 17, 2.3); line(14.8, 17, 20, 17)
            break
        case "mic":
            ctx.moveTo(9.6, 6.4)
            ctx.bezierCurveTo(9.6, 3.2, 14.4, 3.2, 14.4, 6.4)
            ctx.lineTo(14.4, 11.6)
            ctx.bezierCurveTo(14.4, 14.8, 9.6, 14.8, 9.6, 11.6)
            ctx.closePath()
            ctx.moveTo(6.4, 11.4)
            ctx.bezierCurveTo(6.4, 18.2, 17.6, 18.2, 17.6, 11.4)
            line(12, 16.6, 12, 20)
            line(9, 20, 15, 20)
            break
        case "search":
            circle(11, 11, 5.6)
            line(15.2, 15.2, 20, 20)
            break
        case "send":
            ctx.moveTo(3.5, 11); ctx.lineTo(20.5, 4); ctx.lineTo(14, 20.5)
            ctx.lineTo(11.2, 13.3); ctx.closePath()
            ctx.moveTo(11.2, 13.3); ctx.lineTo(20.5, 4)
            break
        case "plus":
            line(12, 5.5, 12, 18.5); line(5.5, 12, 18.5, 12)
            break
        case "x":
            line(6.5, 6.5, 17.5, 17.5); line(17.5, 6.5, 6.5, 17.5)
            break
        case "check":
            ctx.moveTo(5, 12.5); ctx.lineTo(10, 17.5); ctx.lineTo(19, 7)
            break
        case "chevron-left":
            ctx.moveTo(14.5, 5.5); ctx.lineTo(8, 12); ctx.lineTo(14.5, 18.5)
            break
        case "chevron-right":
            ctx.moveTo(9.5, 5.5); ctx.lineTo(16, 12); ctx.lineTo(9.5, 18.5)
            break
        case "chevron-down":
            ctx.moveTo(5.5, 9.5); ctx.lineTo(12, 16); ctx.lineTo(18.5, 9.5)
            break
        case "copy":
            ctx.moveTo(9, 9); ctx.lineTo(20, 9); ctx.lineTo(20, 20); ctx.lineTo(9, 20); ctx.closePath()
            ctx.moveTo(5.5, 15); ctx.lineTo(4, 15); ctx.lineTo(4, 4); ctx.lineTo(15, 4); ctx.lineTo(15, 5.5)
            break
        case "refresh":
            ctx.arc(12, 12, 7.2, -Math.PI * 0.42, Math.PI * 1.18)
            ctx.moveTo(16.6, 3.4); ctx.lineTo(16.4, 7.8); ctx.lineTo(12.2, 6.6)
            break
        case "folder":
            ctx.moveTo(4, 7); ctx.lineTo(9.4, 7); ctx.lineTo(11.4, 9.2); ctx.lineTo(20, 9.2)
            ctx.lineTo(20, 18.6); ctx.lineTo(4, 18.6); ctx.closePath()
            break
        case "trash":
            line(5, 7, 19, 7)
            ctx.moveTo(9.6, 7); ctx.lineTo(9.6, 4.6); ctx.lineTo(14.4, 4.6); ctx.lineTo(14.4, 7)
            ctx.moveTo(6.8, 7); ctx.lineTo(7.8, 20); ctx.lineTo(16.2, 20); ctx.lineTo(17.2, 7)
            line(10.4, 10.4, 10.7, 16.6); line(13.6, 10.4, 13.3, 16.6)
            break
        case "sparkle":
            ctx.moveTo(12, 4); ctx.lineTo(13.9, 10.1); ctx.lineTo(20, 12)
            ctx.lineTo(13.9, 13.9); ctx.lineTo(12, 20); ctx.lineTo(10.1, 13.9)
            ctx.lineTo(4, 12); ctx.lineTo(10.1, 10.1); ctx.closePath()
            break
        case "dot":
            ctx.moveTo(15.2, 12); ctx.arc(12, 12, 3.2, 0, Math.PI * 2)
            ctx.fill()
            break
        }
        ctx.stroke()
    }
}
