import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Chat — azure glass bubbles (01 §5.2).
Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Chat"
            section: "Chat"
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
        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Chat"

            ListView {
                id: log
                anchors.fill: parent
                anchors.margins: Style.gapSm
                clip: true
                spacing: Style.gapSm
                model: app.chatModel
                ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Chat") }

                delegate: Item {
                    id: bubbleRow
                    required property string who
                    required property string text
                    required property bool streaming
                    width: log.width
                    height: bubble.height
                    property bool mine: bubbleRow.who === "you"

                    // List appear fade (ListView owns geometry — no y animation)
                    opacity: 0
                    Component.onCompleted: opacity = 1
                    Behavior on opacity {
                        NumberAnimation {
                            duration: Style.reduceMotion ? Style.durFast : Style.durBase
                            easing.type: Easing.OutCubic
                        }
                    }

                    Rectangle {
                        id: bubble
                        width: Math.min(log.width * 0.82, msg.implicitWidth + Style.pad + (streamCaret.visible ? 16 : 0))
                        height: msg.implicitHeight + 18
                        anchors.right: bubbleRow.mine ? parent.right : undefined
                        anchors.left: bubbleRow.mine ? undefined : parent.left
                        radius: Style.radiusSm
                        // Mine = azure glass gradient; assistant = neutral glass
                        gradient: Gradient {
                            GradientStop {
                                position: 0.0
                                color: bubbleRow.mine
                                       ? Style.tint(Style.sectionColor("Chat"), 0.28)
                                       : Style.glassFillTop
                            }
                            GradientStop {
                                position: 1.0
                                color: bubbleRow.mine
                                       ? Style.tint(Style.sectionColor("Chat"), 0.14)
                                       : Style.glassFillBottom
                            }
                        }
                        // Underlay for glass readability
                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            z: -1
                            color: bubbleRow.mine ? Style.accentDim : Style.surface3
                            opacity: 0.85
                        }
                        border.width: 1
                        border.color: bubbleRow.mine
                                      ? Style.tint(Style.sectionColor("Chat"), 0.40)
                                      : Style.glassBorder

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: Style.gapSm
                            Label {
                                id: msg
                                Layout.fillWidth: true
                                text: bubbleRow.text
                                color: Style.text
                                wrapMode: Text.WordWrap
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsBody
                            }
                            // Streaming caret ▍ (ASCII-safe block) keeps blink
                            Label {
                                id: streamCaret
                                visible: bubbleRow.streaming
                                text: "|"
                                color: Style.sectionColor("Chat")
                                font.bold: true
                                font.pixelSize: Style.fsBody
                                SequentialAnimation on opacity {
                                    running: bubbleRow.streaming
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.2; duration: 500 }
                                    NumberAnimation { to: 1.0; duration: 500 }
                                }
                                // Faint shimmer only when effectsEnabled
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: -2
                                    z: -1
                                    visible: bubbleRow.streaming && Style.effectsEnabled
                                    color: Style.tint(Style.sectionColor("Chat"), 0.20)
                                    radius: 2
                                }
                            }
                        }
                    }
                }
                onCountChanged: positionViewAtEnd()

                EmptyState {
                    anchors.fill: parent
                    visible: log.count === 0
                    glyph: "+"
                    iconName: "chat"
                    glyphColor: Style.sectionColor("Chat")
                    title: "Start a conversation"
                    hint: "Ask anything, or hold Push-to-talk in the sidebar. Try \"where did I leave my keys?\" or \"add milk to the shopping list\"."
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gap
            PmTextField {
                id: input
                Layout.fillWidth: true
                tone: Style.sectionColor("Chat")
                placeholderText: "Ask anything…"
                onAccepted: root.send()
            }
            PmButton {
                text: "Send"
                accent: true
                tone: Style.sectionColor("Chat")
                onClicked: root.send()
            }
        }
    }

    function send() {
        if (input.text.length === 0) return
        app.sendChat(input.text)
        input.text = ""
    }
}
