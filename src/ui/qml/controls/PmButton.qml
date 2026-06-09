import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmButton — a flat/filled dark button.  `accent: true` fills it with the
// primary colour (for the main call-to-action); otherwise it is a quiet
// outlined button.  `flat: true` (inherited) drops the border for nav items.
Button {
    id: control
    property bool accent: false

    implicitHeight: Style.controlH
    padding: 8
    leftPadding: 14
    rightPadding: 14
    font.family: Style.fontFamily
    font.pixelSize: Style.fsBody

    contentItem: Text {
        text: control.text
        font: control.font
        opacity: control.enabled ? 1.0 : 0.4
        color: control.accent ? Style.accentText
             : control.highlighted ? Style.accent
             : Style.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Style.radiusSm
        color: {
            if (control.accent)
                return control.down ? Qt.darker(Style.accent, 1.15) : Style.accent
            if (control.flat)
                return control.highlighted ? Style.accentDim
                     : control.hovered ? Style.surface2 : "transparent"
            return control.down ? Style.surface3
                 : control.hovered ? Style.surface2 : Style.surface
        }
        border.width: control.flat || control.accent ? 0 : 1
        border.color: control.highlighted ? Style.accent : Style.border
        Behavior on color { ColorAnimation { duration: 90 } }
    }
}
