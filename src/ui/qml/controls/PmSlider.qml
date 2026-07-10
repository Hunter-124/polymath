import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmSlider — glass groove, tone fill, glowing handle.
Slider {
    id: control
    property color tone: Style.accent
    from: 0
    to: 1
    value: 0.5
    implicitHeight: 28
    implicitWidth: 200

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 200
        implicitHeight: 6
        width: control.availableWidth
        height: implicitHeight
        radius: 3
        color: Qt.rgba(1, 1, 1, 0.08)

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 3
            color: control.tone
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: 18
        implicitHeight: 18
        radius: 9
        color: Style.text
        border.width: 2
        border.color: control.tone
        Rectangle {
            anchors.centerIn: parent
            width: 28; height: 28; radius: 14
            z: -1
            visible: control.pressed || control.hovered
            color: Style.tint(control.tone, 0.25)
        }
    }
}
