import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

Item {
    id: root

    // Focus the composer whenever the page is shown.
    onVisibleChanged: if (visible) input.forceActiveFocus()

    // True between sending a turn and the first streamed token — drives the
    // "thinking" indicator under the last bubble.
    property bool awaitingReply: false
    Connections {
        target: app
        ignoreUnknownSignals: true
        function onAssistantToken(request_id, text, done) { root.awaitingReply = false }
    }

    // Composer history (Up/Down recall, like a shell).
    property var sentHistory: []
    property int historyPos: 0

    // Clipboard helper — QML has no clipboard API, so route through a hidden
    // TextEdit (the standard trick).
    TextEdit { id: clipProxy; visible: false }
    function copyText(t) {
        clipProxy.text = t
        clipProxy.selectAll()
        clipProxy.copy()
        Nav.notify("good", "Chat", "Copied to clipboard")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "Chat"; color: Style.text
                font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
            }
            Item { Layout.fillWidth: true }
            PmIconButton {
                glyph: "trash"; tip: "Clear conversation"; danger: true
                enabled: log.count > 0
                onClicked: { app.chatModel.clear(); root.awaitingReply = false }
            }
            PmComboBox {
                id: personaBox
                Layout.preferredWidth: 190
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

                // Stick to the newest message only while the user is at the
                // bottom; scrolling up to re-read disables the auto-follow.
                property bool stickToBottom: true
                onMovementEnded: stickToBottom = atYEnd
                onCountChanged: if (stickToBottom) Qt.callLater(positionViewAtEnd)
                onContentHeightChanged: if (stickToBottom) Qt.callLater(positionViewAtEnd)

                delegate: Item {
                    id: bubbleRow
                    required property string who
                    required property string text
                    required property bool streaming
                    required property string timeLabel
                    width: log.width
                    height: bubble.height
                    property bool mine: bubbleRow.who === "you"

                    HoverHandler { id: rowHover }

                    Rectangle {
                        id: bubble
                        width: Math.min(log.width * 0.78, Math.max(msg.implicitWidth + 24, 48))
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
                                // Assistant replies render as Markdown (code, lists,
                                // bold…); user text stays literal.
                                textFormat: bubbleRow.mine ? Text.PlainText : Text.MarkdownText
                                onLinkActivated: link => Qt.openUrlExternally(link)
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

                    // Hover meta: timestamp + copy, beside the bubble.
                    RowLayout {
                        spacing: 2
                        anchors.verticalCenter: bubble.verticalCenter
                        anchors.right: bubbleRow.mine ? bubble.left : undefined
                        anchors.rightMargin: 6
                        anchors.left: bubbleRow.mine ? undefined : bubble.right
                        anchors.leftMargin: 6
                        opacity: rowHover.hovered ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: Style.durFast } }
                        PmIconButton {
                            glyph: "copy"; tip: "Copy"
                            implicitWidth: 24; implicitHeight: 24
                            onClicked: root.copyText(bubbleRow.text)
                        }
                        Label {
                            text: bubbleRow.timeLabel
                            color: Style.textFaint
                            font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                        }
                    }
                }

                // "Thinking…" — shown until the first token lands.
                footer: Item {
                    width: log.width
                    height: root.awaitingReply ? 38 : 0
                    visible: root.awaitingReply
                    Rectangle {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 64; height: 30; radius: Style.radiusSm
                        color: Style.surface3
                        property real phase: 0
                        NumberAnimation on phase {
                            running: root.awaitingReply; loops: Animation.Infinite
                            from: 0; to: Math.PI * 2; duration: 1000
                        }
                        Row {
                            anchors.centerIn: parent; spacing: 6
                            Repeater {
                                model: 3
                                Rectangle {
                                    required property int index
                                    width: 7; height: 7; radius: 3.5
                                    color: Style.textDim
                                    opacity: 0.25 + 0.75 * Math.pow(
                                        Math.max(0, Math.sin(parent.parent.phase - index * 0.9)), 2)
                                }
                            }
                        }
                    }
                }

                // Empty state — invites the first message.
                EmptyState {
                    anchors.fill: parent
                    visible: log.count === 0 && !root.awaitingReply
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
                placeholderText: "Ask anything…   (↑ recalls your last message)"
                onAccepted: root.send()
                Keys.onUpPressed: {
                    if (root.sentHistory.length === 0) return
                    root.historyPos = Math.max(0, root.historyPos - 1)
                    text = root.sentHistory[root.historyPos]
                    cursorPosition = text.length
                }
                Keys.onDownPressed: {
                    if (root.historyPos >= root.sentHistory.length - 1) {
                        root.historyPos = root.sentHistory.length
                        text = ""
                        return
                    }
                    root.historyPos += 1
                    text = root.sentHistory[root.historyPos]
                    cursorPosition = text.length
                }
            }
            PmButton {
                accent: true
                enabled: input.text.trim().length > 0
                onClicked: root.send()
                contentItem: RowLayout {
                    spacing: 7
                    Item { Layout.fillWidth: true }
                    PmIcon { width: 15; height: 15; name: "send"; color: Style.accentText }
                    Text {
                        text: "Send"; color: Style.accentText
                        font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                        verticalAlignment: Text.AlignVCenter
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }

    function send() {
        const t = input.text.trim()
        if (t.length === 0) return
        app.sendChat(t)
        sentHistory.push(t)
        historyPos = sentHistory.length
        input.text = ""
        awaitingReply = true
        log.stickToBottom = true
    }
}
