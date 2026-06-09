import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmSwitch — dark toggle with an accent "on" track.
Switch {
    id: control
    implicitHeight: 26

    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: height / 2
        color: control.checked ? Style.accent : Style.surface3
        border.width: control.checked ? 0 : 1
        border.color: Style.border
        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            y: 3
            width: 18; height: 18; radius: 9
            color: control.checked ? Style.accentText : Style.textDim
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }
}
