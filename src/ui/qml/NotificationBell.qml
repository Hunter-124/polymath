import QtQuick
import Polymath

// NotificationBell — placeholder titlebar control (full impl: B7).
PmToolButton {
    id: root
    property int unreadCount: 0
    iconName: "bell"
    // Optional badge count for future wiring.
    Text {
        anchors.right: parent.right
        anchors.top: parent.top
        visible: root.unreadCount > 0
        text: root.unreadCount > 9 ? "9+" : String(root.unreadCount)
        color: Style.accentText
        font.pixelSize: Style.fsTiny
        font.bold: true
        Rectangle {
            anchors.centerIn: parent
            z: -1
            width: parent.width + 6
            height: parent.height + 4
            radius: height / 2
            color: Style.bad
        }
    }
}
