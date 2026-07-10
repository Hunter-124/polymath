import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmButton — glass-aware accent / plain / flat button (API-compatible).
// contentItem stays overridable (Main.qml nav items replace it).
Button {
    id: control
    property bool accent: false
    property color tone: Style.accent

    implicitHeight: Style.controlH
    padding: 8
    leftPadding: 14
    rightPadding: 14
    font.family: Style.fontFamily
    font.pixelSize: Style.fsBody
    scale: down ? 0.98 : 1.0
    Behavior on scale { NumberAnimation { duration: Style.durFast } }

    contentItem: Text {
        text: control.text
        font: control.font
        opacity: control.enabled ? 1.0 : 0.4
        color: control.accent ? Style.accentText
             : control.highlighted ? control.tone
             : Style.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Style.radiusSm
        gradient: control.accent ? accentGrad : null
        color: {
            if (control.accent)
                return "transparent"
            if (control.flat)
                return control.highlighted ? Style.tint(control.tone, 0.18)
                     : control.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
            return control.down ? Style.surface3
                 : control.hovered ? Style.surface2 : Qt.rgba(1, 1, 1, 0.04)
        }
        border.width: control.flat ? 0 : (control.accent ? 1 : 1)
        border.color: control.accent
            ? Style.tint(control.tone, 0.55)
            : (control.highlighted ? control.tone : Style.glassBorder)
        Behavior on color { ColorAnimation { duration: Style.durFast } }

        Gradient {
            id: accentGrad
            GradientStop { position: 0.0; color: control.tone }
            GradientStop { position: 1.0; color: Qt.darker(control.tone, 1.12) }
        }
    }
}
