import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Chat"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }
            Item { Layout.fillWidth: true }
            ComboBox {
                id: personaBox
                model: app.personalities()
                Component.onCompleted: {
                    var i = model.indexOf(app.activePersonality)
                    if (i >= 0) currentIndex = i
                }
                onActivated: app.setPersonality(currentText)
            }
        }

        // Backed by the C++ ChatModel (app.chatModel). The model coalesces
        // streamed assistant tokens per request id into one bubble.
        ListView {
            id: log
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 8
            model: app.chatModel
            delegate: Rectangle {
                id: bubble
                required property string who
                required property string text
                required property bool streaming
                width: log.width
                height: msg.implicitHeight + 16
                radius: 8
                color: bubble.who === "you" ? "#1f2335" : "#24283b"
                RowLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 8
                    Label {
                        id: msg
                        Layout.fillWidth: true
                        text: (bubble.who === "you" ? "You: " : "Assistant: ") + bubble.text
                        color: "#c0caf5"; wrapMode: Text.WordWrap
                    }
                    // Subtle streaming indicator while tokens are still arriving.
                    Label {
                        visible: bubble.streaming
                        text: "…"; color: "#7aa2f7"; font.bold: true
                    }
                }
            }
            onCountChanged: positionViewAtEnd()
        }

        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: input
                Layout.fillWidth: true
                placeholderText: "Ask anything…"
                onAccepted: root.send()
            }
            Button { text: "Send"; onClicked: root.send() }
        }
    }

    function send() {
        if (input.text.length === 0) return
        app.sendChat(input.text)   // appends the user turn to the model + dispatches
        input.text = ""
    }
}
