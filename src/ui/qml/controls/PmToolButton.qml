import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmToolButton — small icon/glyph button (e.g. the row-delete ✕).
ToolButton {
    id: control
    implicitWidth: 30
    implicitHeight: 30
    font.family: Style.fontFamily
    font.pixelSize: Style.fsBody

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.hovered ? Style.bad : Style.textDim
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: Style.radiusXs
        color: control.hovered ? Style.surface3 : "transparent"
    }
}
