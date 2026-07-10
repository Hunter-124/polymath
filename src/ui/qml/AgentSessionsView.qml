import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Agents — live external CLI session cards (05 §3). Section hue #7FE0FF.
Item {
    id: root
    property var sessions: (typeof agentSessions !== "undefined" && agentSessions)
                           ? agentSessions : null

    Component.onCompleted: {
        if (root.sessions && typeof root.sessions.refresh === "function")
            root.sessions.refresh()
    }

    function statusColor(s) {
        switch (s) {
            case "working":     return Style.sectionColor("Agents")
            case "needs_input": return Style.warn
            case "done":        return Style.good
            case "error":       return Style.bad
            case "stopped":     return Style.textFaint
            default:            return Style.textDim
        }
    }

    function formatCost(c) {
        if (c === undefined || c === null || c <= 0) return ""
        return "$" + Number(c).toFixed(3)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gap
            PmSectionHeader {
                Layout.fillWidth: true
                title: "Agents"
                section: "Agents"
                subtitle: "External AI CLI sessions — Claude Code, Codex, and catch-all PTY. Transparent cowork: reply when a card needs input."
            }
            PmButton {
                text: "New session…"
                accent: true
                tone: Style.sectionColor("Agents")
                onClicked: newDlg.open()
            }
            PmButton {
                text: "Refresh"
                onClicked: if (root.sessions) root.sessions.refresh()
            }
        }

        // Grid of session cards
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: grid.implicitHeight
            clip: true
            ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Agents") }

            GridLayout {
                id: grid
                width: flick.width
                columns: Math.max(1, Math.floor(flick.width / 340))
                rowSpacing: Style.gap
                columnSpacing: Style.gap

                Repeater {
                    model: root.sessions
                    delegate: GlassCard {
                        id: card
                        required property string sessionId
                        required property string provider
                        required property string title
                        required property string cwd
                        required property string status
                        required property string lastMessage
                        required property real costUsd
                        required property string elapsed
                        required property bool unreadPing
                        required property bool experimental

                        Layout.fillWidth: true
                        Layout.preferredHeight: body.implicitHeight + 24
                        section: "Agents"
                        radius: Style.radius

                        // Unread ping glow
                        Rectangle {
                            anchors.fill: parent
                            radius: parent.radius
                            color: "transparent"
                            border.width: card.unreadPing ? 1 : 0
                            border.color: Style.warn
                            z: 3
                            visible: card.unreadPing
                        }

                        ColumnLayout {
                            id: body
                            anchors.fill: parent
                            anchors.margins: Style.gapSm
                            spacing: Style.gapSm
                            z: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Style.gapSm
                                PmStatusDot {
                                    tone: root.statusColor(card.status)
                                    pulsing: card.status === "working"
                                    Layout.alignment: Qt.AlignVCenter
                                }
                                Label {
                                    text: card.title.length ? card.title : card.provider
                                    color: Style.text
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsBody
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                PmBadge {
                                    text: card.provider
                                    tone: Style.sectionColor("Agents")
                                }
                                PmBadge {
                                    visible: card.experimental
                                    text: "experimental"
                                    tone: Style.magenta
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Style.gapSm
                                PmBadge {
                                    text: card.status
                                    tone: root.statusColor(card.status)
                                }
                                Label {
                                    text: card.elapsed
                                    color: Style.textFaint
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsTiny
                                }
                                Label {
                                    visible: card.costUsd > 0
                                    text: root.formatCost(card.costUsd)
                                    color: Style.textDim
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsTiny
                                }
                                Item { Layout.fillWidth: true }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: card.lastMessage.length ? card.lastMessage : "…"
                                color: Style.textDim
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsSmall
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.fillWidth: true
                                text: card.cwd
                                color: Style.textFaint
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny
                                elide: Text.ElideMiddle
                            }

                            // Inline reply
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Style.gapSm
                                visible: card.status === "needs_input" || card.status === "working"
                                PmTextField {
                                    id: replyField
                                    Layout.fillWidth: true
                                    placeholderText: card.status === "needs_input"
                                                     ? "Reply…" : "Send…"
                                    onAccepted: {
                                        if (text.length && root.sessions) {
                                            root.sessions.send(card.sessionId, text)
                                            text = ""
                                            if (root.sessions.clearPing)
                                                root.sessions.clearPing(card.sessionId)
                                        }
                                    }
                                }
                                PmButton {
                                    text: "Reply"
                                    accent: card.status === "needs_input"
                                    tone: Style.sectionColor("Agents")
                                    enabled: replyField.text.length > 0
                                    onClicked: {
                                        if (root.sessions) {
                                            root.sessions.send(card.sessionId, replyField.text)
                                            replyField.text = ""
                                            if (root.sessions.clearPing)
                                                root.sessions.clearPing(card.sessionId)
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Style.gapSm
                                PmButton {
                                    text: "Log"
                                    flat: true
                                    onClicked: {
                                        drillTitle.text = card.title.length ? card.title : card.provider
                                        drillList.model = root.sessions
                                            ? root.sessions.eventLog(card.sessionId) : []
                                        drillDlg.open()
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                PmButton {
                                    text: "Stop"
                                    enabled: card.status === "working" || card.status === "needs_input"
                                    onClicked: if (root.sessions) root.sessions.stop(card.sessionId)
                                }
                            }
                        }
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                width: parent.width * 0.7
                visible: !root.sessions || root.sessions.count === 0
                glyph: ">"
                iconName: "terminal"
                glyphColor: Style.sectionColor("Agents")
                title: "No agent sessions"
                hint: "Spawn Claude Code, Codex, or a PTY profile. Allow directories under Settings ▸ Agents first — empty allowlist means monitor-only."
            }
        }
    }

    // --- New session dialog -------------------------------------------------
    PmDialog {
        id: newDlg
        title: "New agent session"
        section: "Agents"
        property var providerModel: {
            if (!root.sessions) return ["claude-code"]
            var info = root.sessions.availableProviders()
            var names = []
            for (var i = 0; i < info.length; ++i)
                names.push(info[i].name)
            return names.length ? names : ["claude-code"]
        }

        ColumnLayout {
            width: parent ? parent.width : 400
            spacing: Style.gapSm
            Label {
                text: "Provider"
                color: Style.textDim
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
            }
            PmComboBox {
                id: provCombo
                Layout.fillWidth: true
                tone: Style.sectionColor("Agents")
                model: newDlg.providerModel
            }
            Label {
                text: "Working directory (must be under allowed_dirs)"
                color: Style.textDim
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
            }
            PmTextField {
                id: cwdField
                Layout.fillWidth: true
                placeholderText: "C:/work/project"
            }
            Label {
                text: "Title (optional)"
                color: Style.textDim
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
            }
            PmTextField {
                id: titleField
                Layout.fillWidth: true
                placeholderText: "Refactor auth module"
            }
            Label {
                text: "Prompt"
                color: Style.textDim
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
            }
            PmTextField {
                id: promptField
                Layout.fillWidth: true
                placeholderText: "say READY"
            }
            Label {
                id: spawnErr
                Layout.fillWidth: true
                visible: text.length > 0
                color: Style.bad
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
                wrapMode: Text.WordWrap
                text: ""
            }
        }

        footer: [
            PmButton {
                text: "Cancel"
                onClicked: newDlg.close()
            },
            PmButton {
                text: "Spawn"
                accent: true
                tone: Style.sectionColor("Agents")
                onClicked: {
                    spawnErr.text = ""
                    if (!root.sessions) {
                        spawnErr.text = "Sessions model not available"
                        return
                    }
                    var id = root.sessions.spawn(
                        provCombo.currentText || provCombo.displayText,
                        cwdField.text,
                        promptField.text,
                        titleField.text)
                    if (!id || id.length === 0) {
                        spawnErr.text = root.sessions.lastError
                                        || "Spawn refused (check allowed_dirs / concurrency)"
                        return
                    }
                    promptField.text = ""
                    titleField.text = ""
                    newDlg.close()
                }
            }
        ]
    }

    // --- Drill-down event log -----------------------------------------------
    PmDialog {
        id: drillDlg
        title: "Session log"
        section: "Agents"
        width: Math.min(560, (Overlay.overlay ? Overlay.overlay.width : 640) - 48)

        ColumnLayout {
            width: parent ? parent.width : 500
            spacing: Style.gapSm
            Label {
                id: drillTitle
                color: Style.text
                font.family: Style.fontFamily
                font.pixelSize: Style.fsBody
                font.bold: true
            }
            ListView {
                id: drillList
                Layout.fillWidth: true
                Layout.preferredHeight: 280
                clip: true
                spacing: 4
                ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Agents") }
                delegate: Label {
                    required property string modelData
                    width: ListView.view ? ListView.view.width : 0
                    text: modelData
                    color: Style.textDim
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsTiny
                    wrapMode: Text.WrapAnywhere
                }
            }
        }

        footer: [
            PmButton {
                text: "Close"
                onClicked: drillDlg.close()
            }
        ]
    }
}
