import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmItemDelegate — glass selectable row with tone left bar when highlighted.
ItemDelegate {
    id: control
    property color tone: Style.accent
    implicitHeight: 44
    font.family: Style.fontFamily
    font.pixelSize: Style.fsBody

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.highlighted ? control.tone : Style.text
        verticalAlignment: Text.AlignVCenter
        leftPadding: 14
        elide: Text.ElideRight
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Style.radiusSm
            color: control.highlighted ? Style.tint(control.tone, 0.12)
                 : control.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
            border.width: control.highlighted ? 1 : 0
            border.color: Style.tint(control.tone, 0.35)
        }
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 6
            anchors.bottomMargin: 6
            width: 3
            radius: 1.5
            visible: control.highlighted
            color: control.tone
        }
    }
}
