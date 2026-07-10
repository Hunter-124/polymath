import QtQuick
import Polymath

// NotificationBell — titlebar control with unread badge (02 §F3).
PmToolButton {
    id: root
    property int unreadCount: (typeof notifications !== "undefined" && notifications)
                              ? notifications.unreadCount : 0
    property bool open: false
    iconName: "bell"
    tone: Style.accent
    destructive: false

    // Unread badge
    PmBadge {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: -2
        anchors.topMargin: -2
        visible: root.unreadCount > 0
        text: root.unreadCount > 9 ? "9+" : String(root.unreadCount)
        tone: Style.bad
        filled: true
        implicitHeight: 16
        z: 2
    }
}
