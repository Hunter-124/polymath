import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmTextField — dark single-line input with a focus ring.
TextField {
    id: control
    implicitHeight: Style.controlH
    color: Style.text
    placeholderTextColor: Style.textFaint
    selectionColor: Style.accent
    selectedTextColor: Style.accentText
    font.family: Style.fontFamily
    font.pixelSize: Style.fsBody
    leftPadding: 12
    rightPadding: 12
    verticalAlignment: Text.AlignVCenter

    background: Rectangle {
        radius: Style.radiusSm
        color: Style.surface2
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Style.accent : Style.border
        Behavior on border.color { ColorAnimation { duration: 90 } }
    }
}
