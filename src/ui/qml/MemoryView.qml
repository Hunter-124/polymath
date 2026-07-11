import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Memory dashboard — browse / search / delete long-term memories (Wave Z).
Item {
    id: root
    property var rows: []
    property string filter: ""

    function reload() {
        if (typeof app !== "undefined" && app.listMemories)
            rows = app.listMemories(filter, 150)
        else
            rows = []
    }
    Component.onCompleted: reload()
    Connections {
        target: typeof app !== "undefined" ? app : null
        ignoreUnknownSignals: true
        function onMemoriesChanged() { root.reload() }
        function onActiveUserChanged() { root.reload() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Memories"
            section: "Timeline"
            subtitle: "Long-term notes and facts. Search, delete, or add a note."
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gap
            PmTextField {
                id: search
                Layout.fillWidth: true
                tone: Style.sectionColor("Timeline")
                placeholderText: "Search memories…"
                onTextChanged: searchTimer.restart()
                Timer {
                    id: searchTimer
                    interval: 250
                    onTriggered: {
                        root.filter = search.text
                        root.reload()
                    }
                }
            }
            PmButton {
                text: "Refresh"
                onClicked: root.reload()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gap
            PmTextField {
                id: noteField
                Layout.fillWidth: true
                tone: Style.sectionColor("Timeline")
                placeholderText: "Add a note…"
                onAccepted: addNote()
            }
            PmButton {
                text: "Remember"
                accent: true
                onClicked: addNote()
            }
        }

        function addNote() {
            if (!noteField.text.trim()) return
            if (typeof app !== "undefined" && app.rememberNote) {
                app.rememberNote(noteField.text, "note")
                noteField.text = ""
                root.reload()
            }
        }

        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Timeline"

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: Style.gapSm
                clip: true
                spacing: Style.gapSm
                model: root.rows
                ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Timeline") }

                EmptyState {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    title: "No memories yet"
                    hint: "Ask Polymath to remember something, or add a note above."
                }

                delegate: GlassCard {
                    id: mrow
                    required property var modelData
                    width: ListView.view ? ListView.view.width : 0
                    height: col.implicitHeight + 18
                    section: "Timeline"
                    radius: Style.radiusSm

                    ColumnLayout {
                        id: col
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 10
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            PmBadge {
                                text: String(modelData.kind || "note")
                                tone: Style.sectionColor("Timeline")
                            }
                            Item { Layout.fillWidth: true }
                            PmToolButton {
                                text: "Delete"
                                onClicked: {
                                    if (typeof app !== "undefined" && app.deleteMemory) {
                                        app.deleteMemory(modelData.id)
                                        root.reload()
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.text || ""
                            wrapMode: Text.WordWrap
                            color: Style.text
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsBody
                        }
                        Text {
                            text: {
                                var ts = Number(modelData.ts || 0)
                                if (!ts) return ""
                                return new Date(ts * 1000).toLocaleString()
                            }
                            color: Style.textFaint
                            font.pixelSize: Style.fsCaption
                            font.family: Style.fontFamily
                        }
                    }
                }
            }
        }
    }
}
