import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    property string streaming: ""

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
                onActivated: app.setPersonality(currentText)
            }
        }

        ListView {
            id: log
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 8
            model: ListModel { id: messages }
            delegate: Rectangle {
                required property string who
                required property string text
                width: log.width
                height: msg.implicitHeight + 16
                radius: 8
                color: who === "you" ? "#1f2335" : "#24283b"
                Label {
                    id: msg; anchors.fill: parent; anchors.margins: 8
                    text: (who === "you" ? "You: " : "Assistant: ") + parent.text
                    color: "#c0caf5"; wrapMode: Text.WordWrap
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
        messages.append({ who: "you", text: input.text })
        messages.append({ who: "assistant", text: "" })
        root.streaming = ""
        app.sendText(input.text)
        input.text = ""
    }

    Connections {
        target: app
        function onAssistantToken(request_id, text, done) {
            root.streaming += text
            if (messages.count > 0)
                messages.setProperty(messages.count - 1, "text", root.streaming)
        }
    }
}
