import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmScrollBar — glass track, tone thumb, auto-hide.
ScrollBar {
    id: control
    property color tone: Style.accent
    size: 0.3
    policy: ScrollBar.AsNeeded

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: control.pressed ? control.tone
             : control.hovered ? Style.tint(control.tone, 0.7)
             : Style.tint(control.tone, 0.45)
        opacity: control.policy === ScrollBar.AlwaysOn || control.active || control.hovered ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: Style.durFast } }
    }
    background: Rectangle {
        implicitWidth: 8
        implicitHeight: 8
        radius: 4
        color: Qt.rgba(1, 1, 1, 0.04)
        opacity: control.active || control.hovered ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: Style.durFast } }
    }
}
