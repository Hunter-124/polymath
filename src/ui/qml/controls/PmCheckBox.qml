import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmCheckBox — glass box; checked = tone fill + check glyph.
CheckBox {
    id: control
    property color tone: Style.accent
    implicitHeight: 24

    indicator: Rectangle {
        implicitWidth: 20; implicitHeight: 20
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: Style.radiusXs
        color: control.checked ? control.tone : Qt.rgba(1, 1, 1, 0.04)
        border.width: 1
        border.color: control.checked ? control.tone : Style.glassBorder

        // Prefer PmIcon; keep ✓ fallback for safety
        PmIcon {
            anchors.centerIn: parent
            width: 12; height: 12
            visible: control.checked
            name: "check"
            color: Style.accentText
            stroke: 2.2
        }
        Text {
            anchors.centerIn: parent
            visible: false
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
