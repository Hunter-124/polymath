import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmCheckBox — dark checkbox with an accent check.
CheckBox {
    id: control
    implicitHeight: 24

    indicator: Rectangle {
        implicitWidth: 20; implicitHeight: 20
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: Style.radiusXs
        color: control.checked ? Style.accent : Style.surface2
        border.width: 1
        border.color: control.checked ? Style.accent : Style.border

        Text {
            anchors.centerIn: parent
            visible: control.checked
            text: "✓"
            color: Style.accentText
            font.pixelSize: 13
            font.bold: true
        }
    }

    contentItem: Text {
        text: control.text
        leftPadding: control.indicator.width + 8
        color: Style.text
        font.family: Style.fontFamily
        font.pixelSize: Style.fsBody
        verticalAlignment: Text.AlignVCenter
    }
}
