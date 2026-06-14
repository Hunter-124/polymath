import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Polymath

// QuickAsk — the system-wide "ask box" summoned by the global hotkey
// (Ctrl+Alt+Space) even when Hearth is not the focused app.  Type a question,
// Enter to ask; the reply streams in below.  Esc — or clicking away — closes it.
// It routes through app.quickAsk() (the normal chat path), so the exchange also
// lands in the main Chat history.  A focusable top-level window (NOT the
// in-window CommandPalette popup) so it can float over other applications.
Window {
    id: quick
    visible: app.quickAskVisible
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool

    width: 640
    height: card.implicitHeight
    // Centered on the primary screen, sitting in the upper third.
    x: Screen.virtualX + Math.round((Screen.width - width) / 2)
    y: Screen.virtualY + Math.round(Screen.height * 0.22)

    property string rid: ""
    property string answer: ""
    property bool   busy: false
    // Latches true once the window has actually taken focus, so the
    // close-on-deactivate handler doesn't fire on the brief pre-activation frame.
    property bool   wasActive: false

    function submit() {
        const q = field.text.trim()
        if (q.length === 0)
            return
        answer = ""
        busy = true
        rid = app.quickAsk(q)
    }

    onVisibleChanged: {
        if (visible) {
            field.text = ""
            answer = ""
            busy = false
            rid = ""
            wasActive = false
            requestActivate()
            field.forceActiveFocus()
        }
    }

    // Close when focus leaves (e.g. the user clicks another app) — but only after
    // we'd genuinely gained focus, never on the transient pre-activation frame.
    onActiveChanged: {
        if (active)
            wasActive = true
        else if (wasActive && visible)
            app.hideQuickAsk()
    }

    // Stream the reply that matches our request id.
    Connections {
        target: app
        function onAssistantToken(request_id, text, done) {
            if (request_id !== quick.rid)
                return
            quick.answer += text
            if (done)
                quick.busy = false
        }
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: Style.radiusLg
        color: Style.surface
        border.width: 1
        border.color: Style.accent
        implicitHeight: col.implicitHeight + 2 * Style.pad

        // Entrance: fade + a small rise when summoned.
        opacity: quick.visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Style.durMed; easing.type: Style.easeStandard } }

        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: Style.pad
            spacing: Style.gap

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.gap
                PmIcon { width: 18; height: 18; name: "chat"; color: Style.accent }
                TextField {
                    id: field
                    Layout.fillWidth: true
                    placeholderText: "Ask Hearth anything…"
                    color: Style.text
                    placeholderTextColor: Style.textFaint
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsHeading
                    background: null
                    onAccepted: quick.submit()
                    Keys.onEscapePressed: app.hideQuickAsk()
                }
                BusyIndicator {
                    running: quick.busy; visible: quick.busy
                    implicitWidth: 22; implicitHeight: 22
                }
            }

            Rectangle {
                Layout.fillWidth: true; height: 1; color: Style.border
                visible: quick.answer.length > 0 || quick.busy
            }

            Flickable {
                visible: quick.answer.length > 0 || quick.busy
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(answerText.implicitHeight, 320)
                contentHeight: answerText.implicitHeight
                clip: true
                Text {
                    id: answerText
                    width: quick.width - 2 * Style.pad
                    text: quick.answer.length ? quick.answer : "Thinking…"
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    color: quick.answer.length ? Style.text : Style.textFaint
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsBody
                }
            }

            Text {
                Layout.fillWidth: true
                text: "Enter to ask · Esc to close"
                color: Style.textFaint
                font.family: Style.fontFamily
                font.pixelSize: Style.fsTiny
            }
        }
    }
}
