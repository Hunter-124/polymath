import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

ApplicationWindow {
    id: window
    width: 1280; height: 820
    visible: true
    title: "Polymath — Local AI Home Assistant"
    color: Style.bg

    // Bundle Inter (SIL OFL) app-wide. This also silences the
    // "QFontDatabase: Cannot find font directory" warning by guaranteeing a
    // registered family, and makes headless renders match the desktop.
    FontLoader {
        id: inter
        source: "qrc:/qt/qml/Polymath/fonts/Inter.ttf"
        onStatusChanged: if (status === FontLoader.Ready) Style.fontFamily = inter.font.family
    }
    Component.onCompleted: if (inter.status === FontLoader.Ready) Style.fontFamily = inter.font.family
    font.family: Style.fontFamily

    readonly property var pages: [
        { name: "Dashboard",     src: "Dashboard.qml" },
        { name: "Chat",          src: "ChatView.qml" },
        { name: "Cameras",       src: "CamerasView.qml" },
        { name: "Tasks",         src: "TaskQueueView.qml" },
        { name: "Timeline",      src: "TimelineView.qml" },
        { name: "Shopping",      src: "ShoppingView.qml" },
        { name: "Personalities", src: "PersonalitiesView.qml" },
        { name: "Models",        src: "ModelManagerView.qml" },
        { name: "Privacy",       src: "PrivacyView.qml" },
        { name: "Mobile Access", src: "MobileAccessView.qml" }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- Navigation rail ---
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 220
            color: Style.surface
            // hairline separator on the right edge
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Style.border }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 6

                Label {
                    text: "POLYMATH"; color: Style.accent
                    font.family: Style.fontFamily
                    font.pixelSize: 20; font.bold: true; font.letterSpacing: 2
                    Layout.bottomMargin: 4
                }

                // Listening / idle affordance — a pulsing dot + state line.
                Rectangle {
                    Layout.fillWidth: true
                    radius: Style.radiusSm
                    color: Style.surface2
                    implicitHeight: statusCol.implicitHeight + 16
                    border.width: 1
                    border.color: app.listening ? Style.good : Style.border

                    RowLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 8
                        Rectangle {
                            id: pulse
                            width: 10; height: 10; radius: 5
                            color: app.listening ? Style.good : Style.textFaint
                            Layout.alignment: Qt.AlignVCenter
                            SequentialAnimation on opacity {
                                running: app.listening; loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 700; easing.type: Easing.InOutQuad }
                                NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutQuad }
                            }
                        }
                        ColumnLayout {
                            id: statusCol
                            spacing: 1; Layout.fillWidth: true
                            Label {
                                text: app.listening ? "Listening" : "Idle"
                                color: app.listening ? Style.good : Style.textDim
                                font.family: Style.fontFamily; font.pixelSize: Style.fsSmall; font.bold: true
                            }
                            Label {
                                text: app.activePersonality
                                color: Style.textFaint; font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny; elide: Text.ElideRight; Layout.fillWidth: true
                            }
                        }
                    }
                }

                Label {
                    text: app.modelStatus; color: Style.textFaint
                    font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                    elide: Text.ElideRight; Layout.fillWidth: true
                    Layout.bottomMargin: 8; Layout.topMargin: 2
                }

                Repeater {
                    model: window.pages
                    delegate: PmButton {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        flat: true
                        text: modelData.name
                        highlighted: stack.currentIndex === index
                        onClicked: stack.currentIndex = index
                        contentItem: RowLayout {
                            spacing: 10
                            // active-page accent bar
                            Rectangle {
                                width: 3; height: 16; radius: 2
                                color: index === stack.currentIndex ? Style.accent : "transparent"
                            }
                            Text {
                                text: modelData.name
                                font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                                color: index === stack.currentIndex ? Style.accent : Style.text
                                verticalAlignment: Text.AlignVCenter
                                Layout.fillWidth: true
                            }
                        }
                        horizontalPadding: 8
                    }
                }
                Item { Layout.fillHeight: true }

                // Push-to-talk — turns accent + "● talking" while held.
                PmButton {
                    id: ptt
                    Layout.fillWidth: true
                    accent: down
                    text: down ? "●  Listening…" : "Push to talk"
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
            toast.accentColor = level === "error" ? Style.bad
                               : level === "warn" ? Style.warn : Style.accent
            toast.text = source + ": " + message; toast.visible = true; toastTimer.restart()
        }
    }
    Rectangle {
        id: toast
        property alias text: toastLabel.text
        property color accentColor: Style.accent
        visible: false
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 24 }
        width: toastRow.implicitWidth + 28; height: 42; radius: Style.radiusSm; color: Style.surface3
        border.width: 1; border.color: Style.border
        RowLayout {
            id: toastRow; anchors.centerIn: parent; spacing: 10
            Rectangle { width: 6; height: 20; radius: 3; color: toast.accentColor }
            Label { id: toastLabel; color: Style.text; font.family: Style.fontFamily; font.pixelSize: Style.fsBody }
        }
        Timer { id: toastTimer; interval: 4000; onTriggered: toast.visible = false }
    }
}
