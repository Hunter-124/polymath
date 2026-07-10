import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmSwitch — glass track; on = tone gradient. Preserves checked binding.
Switch {
    id: control
    property color tone: Style.accent
    implicitHeight: 26

    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: height / 2
        color: control.checked ? control.tone : Qt.rgba(1, 1, 1, 0.06)
        border.width: control.checked ? 0 : 1
        border.color: Style.glassBorder
        Behavior on color { ColorAnimation { duration: Style.durFast } }

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            y: 3
            width: 18; height: 18; radius: 9
            color: control.checked ? Style.accentText : Style.textDim
            Behavior on x { NumberAnimation { duration: Style.durFast; easing.type: Easing.OutCubic } }
        }
    }
}
