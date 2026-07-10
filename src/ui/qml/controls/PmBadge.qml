import QtQuick
import Polymath

// PmBadge — tokenized tint pill (01 §3.3).
// Note: use `tone` not `color` — Rectangle already owns `color`.
Rectangle {
    id: root
    property string text: ""
    property color tone: Style.accent
    property bool filled: false

    implicitHeight: 22
    implicitWidth: label.implicitWidth + 14
    radius: Style.radiusPill
    color: root.filled ? root.tone : Style.tint(root.tone, 0.16)
    border.width: root.filled ? 0 : 1
    border.color: Style.tint(root.tone, 0.45)

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.filled ? Style.accentText : root.tone
        font.family: Style.fontFamily
        font.pixelSize: Style.fsTiny
        font.bold: true
    }
}
