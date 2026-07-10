import QtQuick
import QtQuick.Layouts
import Polymath

// CommandPalette — placeholder modal (full impl: B8).
Item {
    id: root
    property var actions: []
    property bool open: false
    anchors.fill: parent
    visible: open
    z: 50

    function openPalette() { open = true }
    function close() { open = false }

    Rectangle {
        anchors.fill: parent
        color: Style.overlay
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }
    GlassPanel {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 80
        width: Math.min(560, parent.width - 48)
        height: 120
        radius: Style.radiusLg
        elevation: 3
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Style.padSm
            spacing: Style.gapSm
            Text {
                text: "Command palette"
                color: Style.text
                font.family: Style.fontFamily
                font.pixelSize: Style.fsHeading
                font.bold: true
            }
            Text {
                text: "Ctrl+K stub — fuzzy search + action registry land in B8/C1."
                color: Style.textDim
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
}
