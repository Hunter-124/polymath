import QtQuick
import QtQuick.Layouts
import Polymath

// NotificationCenter — placeholder popover (full impl: B7).
Item {
    id: root
    property bool open: false
    width: 320
    height: open ? 360 : 0
    visible: open
    clip: true

    GlassPanel {
        anchors.fill: parent
        radius: Style.radius
        elevation: 2
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Style.padSm
            Text {
                text: "Notifications"
                color: Style.text
                font.family: Style.fontFamily
                font.pixelSize: Style.fsHeading
                font.bold: true
            }
            Text {
                text: "Center stub — list binds to notifications model in B7."
                color: Style.textDim
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
        }
    }
}
