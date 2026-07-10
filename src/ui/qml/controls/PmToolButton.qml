import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmToolButton — 30×30 glass-on-hover; optional iconName.
// Text-only (legacy ✕ rows): hover → bad, matching previous destructive default.
ToolButton {
    id: control
    property color tone: Style.accent
    property string iconName: ""
    property bool destructive: iconName.length === 0

    implicitWidth: 30
    implicitHeight: 30
    font.family: Style.fontFamily
    font.pixelSize: Style.fsBody

    contentItem: Item {
        PmIcon {
            anchors.centerIn: parent
            width: 16; height: 16
            visible: control.iconName.length > 0
            name: control.iconName
            color: control.hovered
                   ? (control.destructive ? Style.bad : control.tone)
                   : Style.textDim
        }
        Text {
            anchors.centerIn: parent
            visible: control.iconName.length === 0
            text: control.text
            font: control.font
            color: control.hovered ? Style.bad : Style.textDim
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    background: Rectangle {
        radius: Style.radiusXs
        color: control.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
        border.width: control.hovered ? 1 : 0
        border.color: Style.glassBorder
    }
}
