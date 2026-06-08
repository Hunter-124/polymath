import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1280; height: 820
    visible: true
    title: "Polymath — Local AI Home Assistant"
    color: "#0f1115"

    readonly property var pages: [
        { name: "Dashboard",     src: "Dashboard.qml" },
        { name: "Chat",          src: "ChatView.qml" },
        { name: "Cameras",       src: "CamerasView.qml" },
        { name: "Tasks",         src: "TaskQueueView.qml" },
        { name: "Timeline",      src: "TimelineView.qml" },
        { name: "Shopping",      src: "ShoppingView.qml" },
        { name: "Personalities", src: "PersonalitiesView.qml" },
        { name: "Models",        src: "ModelManagerView.qml" },
        { name: "Privacy",       src: "PrivacyView.qml" }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- Navigation rail ---
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 210
            color: "#171a21"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6

                Label {
                    text: "POLYMATH"; color: "#7aa2f7"
                    font.pixelSize: 20; font.bold: true
                    Layout.bottomMargin: 8
                }
                Label {
                    text: (app.listening ? "● listening" : "○ idle") + "  ·  " + app.activePersonality
                    color: app.listening ? "#9ece6a" : "#565f89"; font.pixelSize: 12
                }
                Label { text: app.modelStatus; color: "#565f89"; font.pixelSize: 11
                        Layout.bottomMargin: 8 }

                Repeater {
                    model: window.pages
                    delegate: Button {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        flat: true
                        text: modelData.name
                        highlighted: stack.currentIndex === index
                        onClicked: stack.currentIndex = index
                    }
                }
                Item { Layout.fillHeight: true }
                Button {
                    Layout.fillWidth: true
                    text: "Push to talk"
                    onPressed: app.pushToTalk(true)
                    onReleased: app.pushToTalk(false)
                }
            }
        }

        // --- Page area ---
        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0
            Repeater {
                model: window.pages
                delegate: Loader {
                    required property var modelData
                    source: modelData.src
                    active: true
                }
            }
        }
    }

    // Lightweight toast for backend notices.
    Connections {
        target: app
        function onNoticePosted(level, source, message) {
            toast.text = source + ": " + message; toast.visible = true; toastTimer.restart()
        }
    }
    Rectangle {
        id: toast
        property alias text: toastLabel.text
        visible: false
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 24 }
        width: toastLabel.implicitWidth + 32; height: 40; radius: 8; color: "#24283b"
        Label { id: toastLabel; anchors.centerIn: parent; color: "#c0caf5" }
        Timer { id: toastTimer; interval: 4000; onTriggered: toast.visible = false }
    }
}
