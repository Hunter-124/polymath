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
                            // Read-only selectable body (was Label). TextEdit's own
                            // selectByMouse drag-selection coexists with the ListView's
                            // flick: Qt's TextInput/TextEdit machinery keeps the mouse
                            // grab once a genuine text-selection drag is recognized, so
                            // the enclosing Flickable does not steal it mid-drag: wheel,
                            // the scrollbar, and drags on the RowLayout margins / gaps
                            // between bubbles (not on glyphs) still reach the ListView
                            // untouched. Desktop mouse is the target per the DAG note;
                            // see docs/overhaul2/results/E1_notes.md for the touch caveat.
                            TextEdit {
                                id: msg
                                Layout.fillWidth: true
                                text: bubbleRow.text
                                textFormat: TextEdit.MarkdownText
                                readOnly: true
                                selectByMouse: true
                                persistentSelection: true
                                wrapMode: TextEdit.Wrap
                                color: Style.text
                                selectionColor: Style.sectionColor("Chat")
                                selectedTextColor: Style.accentText
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsBody

                                // Right-click-only overlay so left-button drag still
                                // reaches TextEdit's own selection handling underneath.
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                    onClicked: function (mouse) {
                                        contextMenu.x = mouse.x
                                        contextMenu.y = mouse.y
                                        contextMenu.popup()
                                    }
                                }

                                Menu {
                                    id: contextMenu

                                    background: Rectangle {
                                        implicitWidth: 168
                                        radius: Style.radiusSm
                                        color: Style.surface
                                        border.width: 1
                                        border.color: Style.glassBorder
                                    }

                                    MenuItem {
                                        id: copyItem
                                        text: "Copy"
                                        enabled: msg.selectedText.length > 0
                                        onTriggered: msg.copy()
                                        contentItem: Text {
                                            text: copyItem.text
                                            color: copyItem.enabled ? Style.text : Style.textFaint
                                            font.family: Style.fontFamily
                                            font.pixelSize: Style.fsBody
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: 10
                                            rightPadding: 10
                                        }
                                        background: Rectangle {
                                            implicitHeight: Style.controlHsm
                                            radius: Style.radiusXs
                                            color: copyItem.highlighted
                                                   ? Style.tint(Style.sectionColor("Chat"), 0.16)
                                                   : "transparent"
                                        }
                                    }
                                    MenuItem {
                                        id: selectAllItem
                                        text: "Select All"
                                        onTriggered: msg.selectAll()
                                        contentItem: Text {
                                            text: selectAllItem.text
                                            color: Style.text
                                            font.family: Style.fontFamily
                                            font.pixelSize: Style.fsBody
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: 10
                                            rightPadding: 10
                                        }
                                        background: Rectangle {
                                            implicitHeight: Style.controlHsm
                                            radius: Style.radiusXs
                                            color: selectAllItem.highlighted
                                                   ? Style.tint(Style.sectionColor("Chat"), 0.16)
                                                   : "transparent"
                                        }
                                    }
                                    MenuItem {
                                        id: copyMessageItem
                                        text: "Copy Message"
                                        onTriggered: {
                                            msg.selectAll()
                                            msg.copy()
                                            msg.deselect()
                                        }
                                        contentItem: Text {
                                            text: copyMessageItem.text
                                            color: Style.text
                                            font.family: Style.fontFamily
                                            font.pixelSize: Style.fsBody
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: 10
                                            rightPadding: 10
                                        }
                                        background: Rectangle {
                                            implicitHeight: Style.controlHsm
                                            radius: Style.radiusXs
                                            color: copyMessageItem.highlighted
                                                   ? Style.tint(Style.sectionColor("Chat"), 0.16)
                                                   : "transparent"
                                        }
                                    }
                                }
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
                    hint: "Ask anything, or hold Push-to-talk in the sidebar. Try \"watch this YouTube video\", \"schedule a briefing at 9am\", \"what's on my screen?\", or \"create a file called ideas.txt\"."
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
