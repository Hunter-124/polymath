import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Chat"; color: Style.text
                font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
            }
            Item { Layout.fillWidth: true }
            Label {
                text: "Persona"; color: Style.textFaint
                font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                Layout.alignment: Qt.AlignVCenter
            }
            PmComboBox {
                id: personaBox
                Layout.preferredWidth: 200
                model: app.personalities()
                Component.onCompleted: {
                    var i = model.indexOf(app.activePersonality)
                    if (i >= 0) currentIndex = i
                }
                onActivated: app.setPersonality(currentText)
            }
        }

        // Conversation log, backed by the C++ ChatModel (app.chatModel).
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: Style.radius; color: Style.surface; border.color: Style.borderSoft

            ListView {
                id: log
                anchors.fill: parent; anchors.margins: 10
                clip: true; spacing: 8
                model: app.chatModel
                delegate: Item {
                    id: bubbleRow
                    required property string who
                    required property string text
                    required property bool streaming
                    width: log.width
                    height: bubble.height
                    property bool mine: bubbleRow.who === "you"

                    Rectangle {
                        id: bubble
                        width: Math.min(log.width * 0.82, msg.implicitWidth + 24)
                        height: msg.implicitHeight + 18
                        anchors.right: bubbleRow.mine ? parent.right : undefined
                        anchors.left: bubbleRow.mine ? undefined : parent.left
                        radius: Style.radiusSm
                        color: bubbleRow.mine ? Style.accentDim : Style.surface3
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 9; spacing: 8
                            Label {
                                id: msg
                                Layout.fillWidth: true
                                text: bubbleRow.text
                                color: Style.text; wrapMode: Text.WordWrap
                                font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                            }
                            // Streaming indicator while tokens are still arriving.
                            Label {
                                visible: bubbleRow.streaming
                                text: "▍"; color: Style.accent; font.bold: true
                                SequentialAnimation on opacity {
                                    running: bubbleRow.streaming; loops: Animation.Infinite
                                    NumberAnimation { to: 0.2; duration: 500 }
                                    NumberAnimation { to: 1.0; duration: 500 }
                                }
                            }
                        }
                    }
                }
                onCountChanged: positionViewAtEnd()

                // Empty state — invites the first message.
                EmptyState {
                    anchors.fill: parent
                    visible: log.count === 0
                    glyph: "+"
                    glyphColor: Style.accent
                    title: "Start a conversation"
                    hint: "Ask anything, or hold Push-to-talk in the sidebar. Try \"where did I leave my keys?\" or \"add milk to the shopping list\"."
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: Style.gap
            PmTextField {
                id: input
                Layout.fillWidth: true
                placeholderText: "Ask anything…"
                onAccepted: root.send()
            }
            PmButton { text: "Send"; accent: true; onClicked: root.send() }
        }
    }

    function send() {
        if (input.text.length === 0) return
        app.sendChat(input.text)
        input.text = ""
    }
}
