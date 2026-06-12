import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmIconButton — a compact square button hosting a PmIcon, with an optional
// themed tooltip (`tip`).  `danger: true` tints the hover state red (delete
// affordances); `accent: true` keeps the icon accent-coloured at rest.
ToolButton {
    id: control
    property string glyph: ""
    property string tip: ""
    property bool   danger: false
    property bool   accent: false

    implicitWidth: 30
    implicitHeight: 30

    contentItem: Item {
        PmIcon {
            anchors.centerIn: parent
            width: 17; height: 17
            name: control.glyph
            color: !control.enabled ? Style.textFaint
                 : control.danger && control.hovered ? Style.bad
                 : control.accent ? Style.accent
                 : control.hovered ? Style.text : Style.textDim
        }
    }

    background: Rectangle {
        radius: Style.radiusXs
        color: control.down ? Style.surface3
             : control.hovered ? Style.surface2 : "transparent"
        Behavior on color { ColorAnimation { duration: Style.durFast } }
    }

    ToolTip {
        visible: control.hovered && control.tip.length > 0
        delay: 550
        text: control.tip
        contentItem: Text {
            text: control.tip
            color: Style.text
            font.family: Style.fontFamily
            font.pixelSize: Style.fsSmall
        }
        background: Rectangle {
            radius: Style.radiusXs
            color: Style.surface3
            border.width: 1
            border.color: Style.border
        }
    }
}
