import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// PanelMode — fullscreen, touch-optimised kiosk/wall-panel dashboard.
//
// Loaded by main.cpp when --panel is passed; replaces Main.qml as the root
// window.  Like every other view it never reaches into Main.qml — all
// navigation goes through Nav, and every AppController binding is a context
// property ("app", "chatModel", "taskModel", "cameraModel", "timelineModel").
//
// Layout (portrait or landscape, scales with window size):
//
//   ┌─────────────────────────────────────────────────────┐
//   │  HEARTH   ● Idle / Listening    [personality]  [PTT]│  header bar
//   ├──────────────┬──────────────────────────────────────┤
//   │  TASKS       │  CAMERA STRIP                        │
//   │  (large rows)│  (horizontal scroll of live tiles)   │
//   │              │                                      │
//   ├──────────────┴──────────────────────────────────────┤
//   │  CHAT                                               │
//   │  (conversation log + composer, streaming-aware)     │
//   └─────────────────────────────────────────────────────┘
//
// All hit targets are >= 56 px tall (touch-safe). Type is large.
// Styling stays on the same dark Tokyo-Night palette (Style singleton).

ApplicationWindow {
    id: panelWindow
    visible: true
    // Default size for windowed/offscreen use; real panels run fullscreen.
    width: 1280
    height: 800
    visibility: Window.FullScreen
    title: "Hearth — Panel Mode"
    color: Style.bg

    // Bundle Inter app-wide, exactly as Main.qml does.
    FontLoader {
        id: inter
        source: "qrc:/qt/qml/Polymath/fonts/Inter.ttf"
        onStatusChanged: if (status === FontLoader.Ready) Style.fontFamily = inter.font.family
    }
    Component.onCompleted: {
        if (inter.status === FontLoader.Ready) Style.fontFamily = inter.font.family
        app.refreshAll()
    }
    font.family: Style.fontFamily

    // Wake-on-touch: if the screen goes to sleep (brightness 0) a tap wakes it.
    // We simply keep the window always-on-top and never let the screen saver
    // engage — actual screen-sleep is handled at OS level in a real kiosk.

    // -------------------------------------------------------------------------
    //  Shared: awaiting-reply state for the chat panel
    // -------------------------------------------------------------------------
    property bool awaitingReply: false
    Connections {
        target: app
        ignoreUnknownSignals: true
        function onAssistantToken(request_id, text, done) {
            panelWindow.awaitingReply = false
        }
        function onNoticePosted(level, source, message) {
            panelToast(level, source, message)
        }
    }

    // Camera refresh tick (mirrors CamerasView).
    property int refreshTick: 0
    Timer { interval: 1000; running: true; repeat: true; onTriggered: panelWindow.refreshTick++ }

    // Nav signals: navigate = ignored in full-screen panel (no shell); notify = toast.
    Connections {
        target: Nav
        function onNotify(level, source, message) { panelWindow.panelToast(level, source, message) }
        function onFocusChat() { chatInput.forceActiveFocus() }
    }

    // =========================================================================
    //  HEADER BAR
    // =========================================================================
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: header
            Layout.fillWidth: true
            implicitHeight: 64
            color: Style.surface
            Rectangle { anchors.bottom: parent.bottom; height: 1; width: parent.width; color: Style.border }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 16

                // Brand monogram + wordmark.
                RowLayout {
                    spacing: 10
                    Rectangle {
                        width: 34; height: 34; radius: 10
                        color: Style.accentDim
                        PmIcon { anchors.centerIn: parent; width: 20; height: 20; name: "sparkle"; color: Style.accent }
                    }
                    Label {
                        text: "HEARTH"; color: Style.accent
                        font.family: Style.fontFamily; font.pixelSize: 20; font.bold: true; font.letterSpacing: 2
                    }
                }

                Item { Layout.fillWidth: true }

                // Listening status pill — fronted by the active persona's face.
                Rectangle {
                    radius: height / 2
                    color: Style.surface2
                    border.width: 1
                    border.color: app.speaking ? Style.accent : app.listening ? Style.good : Style.border
                    Behavior on border.color { ColorAnimation { duration: Style.durMed } }
                    implicitWidth: statusRow.implicitWidth + 24
                    implicitHeight: 44

                    RowLayout {
                        id: statusRow
                        anchors.centerIn: parent
                        spacing: 10

                        PersonalityAvatar {
                            Layout.preferredWidth: 30; Layout.preferredHeight: 30
                            Layout.alignment: Qt.AlignVCenter
                            displayName: app.activePersona.name || app.activePersonality
                            avatarStyle: app.activePersona.style || "orb"
                            accent: (app.activePersona.accent && app.activePersona.accent.length)
                                    ? app.activePersona.accent : Style.accent
                            idleSource: app.activePersona.idle || ""
                            talkingSource: app.activePersona.talking || ""
                            speaking: app.speaking || panelWindow.awaitingReply
                        }
                        ColumnLayout {
                            spacing: 1
                            Label {
                                text: app.speaking ? "Speaking" : app.listening ? "Listening" : "Idle"
                                color: app.speaking ? Style.accent : app.listening ? Style.good : Style.textDim
                                font.family: Style.fontFamily; font.pixelSize: 15; font.bold: true
                            }
                            Label {
                                text: app.activePersonality
                                color: Style.textFaint
                                font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                elide: Text.ElideRight
                                Layout.preferredWidth: Math.min(implicitWidth, 160)
                            }
                        }
                    }
                }

                // Model status — faint; taps into Settings > Models in normal flow
                // (Nav.goSettings doesn't exist in panel mode as a nav target, but
                // the signal is harmlessly unconnected).
                Label {
                    text: app.modelStatus
                    color: app.hasModels ? Style.textFaint : Style.warn
                    font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                    elide: Text.ElideRight
                    Layout.preferredWidth: Math.min(implicitWidth, 200)
                }

                // Push-to-talk button — big for finger use.
                AbstractButton {
                    id: ptt
                    implicitWidth: pttRow.implicitWidth + 28
                    implicitHeight: 48
                    onPressed:  app.pushToTalk(true)
                    onReleased: app.pushToTalk(false)

                    background: Rectangle {
                        radius: Style.radiusSm
                        color: ptt.down ? Style.accent : Style.surface2
                        border.width: 1
                        border.color: ptt.down ? Style.accent : Style.border
                        Behavior on color { ColorAnimation { duration: Style.durFast } }
                    }
                    contentItem: RowLayout {
                        id: pttRow
                        spacing: 8
                        PmIcon {
                            width: 20; height: 20; name: "mic"
                            color: ptt.down ? Style.accentText : Style.textDim
                        }
                        Label {
                            text: ptt.down ? "Listening…" : "Push to talk"
                            color: ptt.down ? Style.accentText : Style.text
                            font.family: Style.fontFamily; font.pixelSize: 15
                        }
                    }
                }
            }
        }

        // =====================================================================
        //  MAIN CONTENT: Tasks | Camera strip  (top half) + Chat (bottom half)
        // =====================================================================
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Horizontal split: tasks on the left ~30%, cameras on the right ~70%.
            RowLayout {
                anchors.fill: parent
                spacing: 0

                // =============================================================
                //  LEFT — TASKS PANEL
                // =============================================================
                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: Math.round(panelWindow.width * 0.28)
                    color: Style.surface
                    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Style.border }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            PmIcon { width: 22; height: 22; name: "tasks"; color: Style.accent }
                            Label {
                                text: "Tasks"; color: Style.text
                                font.family: Style.fontFamily; font.pixelSize: Style.fsHeading; font.bold: true
                                Layout.fillWidth: true
                            }
                            // Running count chip.
                            Rectangle {
                                visible: taskModel.runningCount > 0
                                radius: height / 2
                                color: Qt.rgba(Style.accent.r, Style.accent.g, Style.accent.b, 0.14)
                                implicitWidth: runChipLbl.implicitWidth + 16; implicitHeight: 26
                                Label {
                                    id: runChipLbl
                                    anchors.centerIn: parent
                                    text: taskModel.runningCount + " running"
                                    color: Style.accent
                                    font.family: Style.fontFamily; font.pixelSize: Style.fsSmall; font.bold: true
                                }
                            }
                        }

                        ListView {
                            id: taskList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 8
                            model: taskModel

                            delegate: Rectangle {
                                id: trow
                                required property string type
                                required property string status
                                required property string detail
                                required property int priority
                                width: taskList.width
                                radius: Style.radiusSm
                                color: Style.surface2
                                implicitHeight: tCol.implicitHeight + 20

                                function statusColor(s) {
                                    switch (s) {
                                        case "running":  return Style.accent
                                        case "done":     return Style.good
                                        case "error":    return Style.bad
                                        case "canceled": return Style.textFaint
                                        default:         return Style.warn
                                    }
                                }

                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 14; spacing: 12

                                    Rectangle {
                                        width: 12; height: 12; radius: 6
                                        color: trow.statusColor(trow.status)
                                        Layout.alignment: Qt.AlignTop; Layout.topMargin: 4
                                        SequentialAnimation on opacity {
                                            running: trow.status === "running"; loops: Animation.Infinite
                                            NumberAnimation { to: 0.3; duration: 650 }
                                            NumberAnimation { to: 1.0; duration: 650 }
                                        }
                                    }

                                    ColumnLayout {
                                        id: tCol
                                        Layout.fillWidth: true; spacing: 4
                                        RowLayout {
                                            Layout.fillWidth: true; spacing: 8
                                            Label {
                                                text: trow.type; color: Style.text
                                                font.family: Style.fontFamily; font.pixelSize: Style.fsBody; font.bold: true
                                                elide: Text.ElideRight; Layout.fillWidth: true
                                            }
                                            Rectangle {
                                                radius: Style.radiusXs
                                                color: Qt.rgba(trow.statusColor(trow.status).r,
                                                               trow.statusColor(trow.status).g,
                                                               trow.statusColor(trow.status).b, 0.18)
                                                implicitWidth: stLbl.implicitWidth + 12; implicitHeight: stLbl.implicitHeight + 6
                                                Label {
                                                    id: stLbl; anchors.centerIn: parent
                                                    text: trow.status; color: trow.statusColor(trow.status)
                                                    font.family: Style.fontFamily; font.pixelSize: Style.fsTiny; font.bold: true
                                                }
                                            }
                                        }
                                        Label {
                                            visible: trow.detail.length > 0
                                            text: trow.detail; color: Style.textDim
                                            font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                            wrapMode: Text.WordWrap; Layout.fillWidth: true
                                            maximumLineCount: 3; elide: Text.ElideRight
                                        }
                                    }
                                }
                            }

                            // Empty state (small glyph, readable at panel distance).
                            EmptyState {
                                anchors.fill: parent
                                visible: taskList.count === 0
                                glyph: "●"
                                title: "No tasks queued"
                                hint: "Ask Hearth for something heavy and it appears here."
                            }
                        }
                    }
                }

                // =============================================================
                //  RIGHT — top: camera strip, bottom: chat
                // =============================================================
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    // ---------------------------------------------------------
                    //  CAMERA STRIP
                    // ---------------------------------------------------------
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.round(panelWindow.height * 0.32)
                        color: Style.bg
                        Rectangle { anchors.bottom: parent.bottom; height: 1; width: parent.width; color: Style.border }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true; spacing: 8
                                PmIcon { width: 20; height: 20; name: "camera"; color: Style.accent }
                                Label {
                                    text: "Live Cameras"; color: Style.text
                                    font.family: Style.fontFamily; font.pixelSize: Style.fsBody; font.bold: true
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: Qt.formatTime(new Date(), "hh:mm")
                                    color: Style.textFaint
                                    font.family: Style.fontFamily; font.pixelSize: Style.fsSmall

                                    Timer {
                                        interval: 30000; running: true; repeat: true
                                        onTriggered: parent.text = Qt.formatTime(new Date(), "hh:mm")
                                    }
                                }
                            }

                            ListView {
                                id: cameraStrip
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                orientation: ListView.Horizontal
                                spacing: 10
                                clip: true
                                model: cameraModel

                                delegate: Rectangle {
                                    id: camTile
                                    required property int    cameraId
                                    required property string name
                                    required property string location
                                    required property bool   enabled
                                    required property bool   live
                                    required property int    frameTick

                                    // Height fills the ListView; width is 16:9 relative to that.
                                    height: cameraStrip.height
                                    width: Math.round(height * 1.44)
                                    radius: Style.radiusSm
                                    color: Style.surface2
                                    border.color: camTile.live ? Style.good : Style.border
                                    border.width: camTile.live ? 2 : 1

                                    ColumnLayout {
                                        anchors.fill: parent; anchors.margins: 6; spacing: 5

                                        Rectangle {
                                            Layout.fillWidth: true; Layout.fillHeight: true
                                            radius: Style.radiusXs; color: "#0b0d12"; clip: true

                                            Image {
                                                anchors.fill: parent
                                                fillMode: Image.PreserveAspectFit
                                                asynchronous: true
                                                cache: false
                                                source: camTile.enabled
                                                    ? "image://cameras/" + camTile.cameraId + "?t=" + camTile.frameTick + "_" + panelWindow.refreshTick
                                                    : ""
                                            }
                                            Label {
                                                anchors.centerIn: parent; visible: !camTile.enabled
                                                text: "○  disabled"; color: Style.textFaint
                                                font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                            }
                                            Label {
                                                anchors.centerIn: parent; visible: camTile.enabled && !camTile.live
                                                text: "○  connecting…"; color: Style.textFaint
                                                font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true; spacing: 6
                                            Label {
                                                text: camTile.name + (camTile.location.length ? "  ·  " + camTile.location : "")
                                                color: Style.text; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                                elide: Text.ElideRight; Layout.fillWidth: true
                                            }
                                            Label {
                                                text: camTile.live ? "● live" : "○"
                                                color: camTile.live ? Style.good : Style.textFaint
                                                font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                                            }
                                        }
                                    }
                                }

                                EmptyState {
                                    anchors.fill: parent
                                    visible: cameraStrip.count === 0
                                    glyph: "○"
                                    title: "No cameras"
                                    hint: "Flash the ESP32-CAM firmware and connect cameras."
                                }
                            }
                        }
                    }

                    // ---------------------------------------------------------
                    //  CHAT PANEL  (fills remaining vertical space)
                    // ---------------------------------------------------------
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            RowLayout {
                                Layout.fillWidth: true; spacing: 8
                                PmIcon { width: 20; height: 20; name: "chat"; color: Style.accent }
                                Label {
                                    text: "Chat"; color: Style.text
                                    font.family: Style.fontFamily; font.pixelSize: Style.fsBody; font.bold: true
                                    Layout.fillWidth: true
                                }
                                PmIconButton {
                                    glyph: "trash"; tip: "Clear conversation"; danger: true
                                    implicitWidth: 44; implicitHeight: 44
                                    enabled: chatLog.count > 0
                                    onClicked: { app.chatModel.clear(); panelWindow.awaitingReply = false }
                                }
                            }

                            // Conversation log.
                            Rectangle {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                radius: Style.radius; color: Style.surface; border.color: Style.borderSoft

                                ListView {
                                    id: chatLog
                                    anchors.fill: parent; anchors.margins: 10
                                    clip: true; spacing: 8
                                    model: app.chatModel

                                    property bool stickToBottom: true
                                    onMovementEnded: stickToBottom = atYEnd
                                    onCountChanged: if (stickToBottom) Qt.callLater(positionViewAtEnd)
                                    onContentHeightChanged: if (stickToBottom) Qt.callLater(positionViewAtEnd)

                                    delegate: Item {
                                        id: bubbleRow
                                        required property string who
                                        required property string text
                                        required property bool   streaming
                                        required property string timeLabel
                                        width: chatLog.width
                                        height: bubble.height
                                        property bool mine: bubbleRow.who === "you"

                                        Rectangle {
                                            id: bubble
                                            // Touch-appropriate bubble: slightly wider, taller line height.
                                            width: Math.min(chatLog.width * 0.82, Math.max(msg.implicitWidth + 28, 56))
                                            height: msg.implicitHeight + 22
                                            anchors.right: bubbleRow.mine ? parent.right : undefined
                                            anchors.left:  bubbleRow.mine ? undefined    : parent.left
                                            radius: Style.radiusSm
                                            color: bubbleRow.mine ? Style.accentDim : Style.surface3

                                            RowLayout {
                                                anchors.fill: parent; anchors.margins: 11; spacing: 8
                                                Label {
                                                    id: msg
                                                    Layout.fillWidth: true
                                                    text: bubbleRow.text
                                                    color: Style.text; wrapMode: Text.WordWrap
                                                    font.family: Style.fontFamily; font.pixelSize: 15
                                                    textFormat: bubbleRow.mine ? Text.PlainText : Text.MarkdownText
                                                    onLinkActivated: link => Qt.openUrlExternally(link)
                                                }
                                                // Streaming cursor.
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

                                        // Timestamp floats beside the bubble.
                                        Label {
                                            text: bubbleRow.timeLabel
                                            color: Style.textFaint
                                            font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                                            anchors.verticalCenter: bubble.verticalCenter
                                            anchors.right: bubbleRow.mine ? bubble.left : undefined
                                            anchors.rightMargin: 8
                                            anchors.left:  bubbleRow.mine ? undefined   : bubble.right
                                            anchors.leftMargin: 8
                                        }
                                    }

                                    // "Thinking…" indicator.
                                    footer: Item {
                                        width: chatLog.width
                                        height: panelWindow.awaitingReply ? 44 : 0
                                        visible: panelWindow.awaitingReply
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 70; height: 34; radius: Style.radiusSm
                                            color: Style.surface3
                                            property real phase: 0
                                            NumberAnimation on phase {
                                                running: panelWindow.awaitingReply; loops: Animation.Infinite
                                                from: 0; to: Math.PI * 2; duration: 1000
                                            }
                                            Row {
                                                anchors.centerIn: parent; spacing: 7
                                                Repeater {
                                                    model: 3
                                                    Rectangle {
                                                        required property int index
                                                        width: 8; height: 8; radius: 4
                                                        color: Style.textDim
                                                        opacity: 0.25 + 0.75 * Math.pow(
                                                            Math.max(0, Math.sin(parent.parent.phase - index * 0.9)), 2)
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    EmptyState {
                                        anchors.fill: parent
                                        visible: chatLog.count === 0 && !panelWindow.awaitingReply
                                        glyph: "+"
                                        glyphColor: Style.accent
                                        title: "Start a conversation"
                                        hint: "Tap the composer below, or hold Push-to-talk."
                                    }
                                }
                            }

                            // Composer row — tall for touch.
                            RowLayout {
                                Layout.fillWidth: true; spacing: Style.gap

                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 56
                                    radius: Style.radiusSm
                                    color: Style.surface2
                                    border.width: chatInput.activeFocus ? 2 : 1
                                    border.color: chatInput.activeFocus ? Style.accent : Style.border
                                    Behavior on border.color { ColorAnimation { duration: 90 } }

                                    TextInput {
                                        id: chatInput
                                        anchors.fill: parent
                                        anchors.leftMargin: 16; anchors.rightMargin: 16
                                        verticalAlignment: Text.AlignVCenter
                                        color: Style.text
                                        selectionColor: Style.accent
                                        selectedTextColor: Style.accentText
                                        font.family: Style.fontFamily; font.pixelSize: 15
                                        clip: true

                                        Label {
                                            anchors.fill: parent
                                            verticalAlignment: Text.AlignVCenter
                                            text: "Ask anything…"
                                            color: Style.textFaint
                                            font.family: Style.fontFamily; font.pixelSize: 15
                                            visible: chatInput.text.length === 0 && !chatInput.activeFocus
                                        }

                                        Keys.onReturnPressed: panelWindow.sendChat()
                                        Keys.onEnterPressed:  panelWindow.sendChat()
                                    }
                                }

                                // Send button — large touch target.
                                AbstractButton {
                                    id: sendBtn
                                    implicitWidth: 80; implicitHeight: 56
                                    enabled: chatInput.text.trim().length > 0
                                    onClicked: panelWindow.sendChat()

                                    background: Rectangle {
                                        radius: Style.radiusSm
                                        color: sendBtn.enabled
                                               ? (sendBtn.down ? Qt.darker(Style.accent, 1.15) : Style.accent)
                                               : Style.surface2
                                        Behavior on color { ColorAnimation { duration: Style.durFast } }
                                    }
                                    contentItem: RowLayout {
                                        spacing: 6
                                        Item { Layout.fillWidth: true }
                                        PmIcon {
                                            width: 20; height: 20; name: "send"
                                            color: sendBtn.enabled ? Style.accentText : Style.textFaint
                                        }
                                        Label {
                                            text: "Send"
                                            color: sendBtn.enabled ? Style.accentText : Style.textFaint
                                            font.family: Style.fontFamily; font.pixelSize: 15; font.bold: true
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // =========================================================================
    //  Toast stack — identical logic to Main.qml
    // =========================================================================
    function panelToast(level, source, message) {
        if (toastModel.count >= 4) toastModel.remove(0)
        toastModel.append({ level: level, source: source, message: message })
    }

    ListModel { id: toastModel }

    ColumnLayout {
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 24 }
        spacing: 8
        Repeater {
            model: toastModel
            delegate: Rectangle {
                id: toast
                required property int    index
                required property string level
                required property string source
                required property string message
                readonly property color tone: level === "error" ? Style.bad
                                            : level === "warn"  ? Style.warn
                                            : level === "good"  ? Style.good : Style.accent
                Layout.alignment: Qt.AlignHCenter
                width: Math.min(toastRow.implicitWidth + 28, panelWindow.width - 80)
                height: 48; radius: Style.radiusSm; color: Style.surface3
                border.width: 1; border.color: Style.border
                opacity: 0
                Component.onCompleted: opacity = 1
                Behavior on opacity { NumberAnimation { duration: Style.durMed } }

                RowLayout {
                    id: toastRow
                    anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 8
                    spacing: 12
                    Rectangle { width: 6; height: 20; radius: 3; color: toast.tone }
                    Label {
                        text: (toast.source.length ? toast.source + ":  " : "") + toast.message
                        color: Style.text; font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                        elide: Text.ElideRight; Layout.fillWidth: true
                    }
                    PmIconButton {
                        glyph: "x"; implicitWidth: 32; implicitHeight: 32
                        onClicked: if (toast.index < toastModel.count) toastModel.remove(toast.index)
                    }
                }
                Timer {
                    interval: 4500 + toast.index * 300; running: true
                    onTriggered: if (toast.index < toastModel.count) toastModel.remove(toast.index)
                }
            }
        }
    }

    // =========================================================================
    //  sendChat — matches ChatView.send() behaviour exactly
    // =========================================================================
    function sendChat() {
        const t = chatInput.text.trim()
        if (t.length === 0) return
        app.sendChat(t)
        chatInput.text = ""
        awaitingReply = true
        chatLog.stickToBottom = true
    }
}
