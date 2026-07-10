import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmTextField — glass single-line input with tone focus ring. Keeps onAccepted.
TextField {
    id: control
    property color tone: Style.accent

    implicitHeight: Style.controlH
    color: Style.text
    placeholderTextColor: Style.textFaint
    selectionColor: control.tone
    selectedTextColor: Style.accentText
    font.family: Style.fontFamily
    font.pixelSize: Style.fsBody
    leftPadding: 12
    rightPadding: 12
    verticalAlignment: Text.AlignVCenter

    background: Rectangle {
        radius: Style.radiusSm
        color: Qt.rgba(1, 1, 1, 0.04)
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? control.tone : Style.glassBorder
        // Soft outer glow when effects on + focused
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            z: -1
            visible: control.activeFocus && Style.effectsEnabled
            color: "transparent"
            border.width: 3
            border.color: Style.tint(control.tone, 0.25)
        }
        Behavior on border.color { ColorAnimation { duration: Style.durFast } }
    }
}
