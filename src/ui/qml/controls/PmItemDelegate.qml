import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmItemDelegate — dark selectable list row used by list-style views.
ItemDelegate {
    id: control
    implicitHeight: 44
    font.family: Style.fontFamily
    font.pixelSize: Style.fsBody

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.highlighted ? Style.accent : Style.text
        verticalAlignment: Text.AlignVCenter
        leftPadding: 12
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Style.radiusSm
        color: control.highlighted ? Style.accentDim
             : control.hovered ? Style.surface2 : Style.surface
        border.width: control.highlighted ? 1 : 0
        border.color: Style.accent
    }
}
