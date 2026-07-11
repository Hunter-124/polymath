import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Personalities — magenta glass card list (01 §5.7) + in-GUI editor (E2).
// Create / edit / duplicate / delete persona bundles without touching disk.
// editIndex: -1 = list, -2 = new-persona editor, >=0 = editing that row.
Item {
    id: root
    property int editIndex: -1
    property string pendingDeleteName: ""

    function openNew() { root.editIndex = -2 }
    function openEdit(row) { root.editIndex = row }
    function closeEditor() { root.editIndex = -1 }

    // ---- list ---------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap
        visible: root.editIndex === -1

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gap
            PmSectionHeader {
                Layout.fillWidth: true
                title: "Personalities"
                section: "Personalities"
                subtitle: "Modular historical minds. Create, edit, and retire persona bundles right here — or drop a folder into  personalities/<name>/persona.json  and it hot-loads."
            }
            PmButton {
                text: "+ New"
                accent: true
                tone: Style.sectionColor("Personalities")
                onClicked: root.openNew()
            }
        }

        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Personalities"

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: Style.gapSm
                clip: true
                spacing: Style.gapSm
                model: personalityModel
                ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Personalities") }

                delegate: GlassCard {
                    id: card
                    required property int index
                    required property string name
                    required property string systemPrompt
                    required property string voice
                    required property string preferredModel
                    required property string avatarPath
                    required property bool isActive
                    width: ListView.view ? ListView.view.width : 0
                    height: 78
                    section: "Personalities"
                    radius: Style.radiusSm

                    Rectangle {
                        anchors.fill: parent
                        radius: card.radius
                        color: "transparent"
                        border.width: card.isActive ? 1 : 0
                        border.color: Style.good
                        z: 3
                    }

                    // Whole-row click activates (kept from the read-only view);
                    // the action buttons sit above it and take priority.
                    MouseArea {
                        anchors.fill: parent
                        z: 0
                        cursorShape: Qt.PointingHandCursor
                        onClicked: app.setPersonality(card.name)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Style.gapSm
                        spacing: Style.gapSm
                        z: 2

                        Rectangle {
                            width: 46; height: 46; radius: 23
                            color: Style.tint(Style.sectionColor("Personalities"), 0.16)
                            border.width: 1
                            border.color: Style.tint(Style.sectionColor("Personalities"), 0.45)

                            Image {
                                anchors.fill: parent
                                anchors.margins: 1
                                visible: card.avatarPath.length > 0
                                source: card.avatarPath.length > 0 ? "file:///" + card.avatarPath : ""
                                fillMode: Image.PreserveAspectCrop
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: card.avatarPath.length === 0
                                text: card.name.length > 0 ? card.name.charAt(0).toUpperCase() : "?"
                                color: Style.sectionColor("Personalities")
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsHeading
                                font.bold: true
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                spacing: Style.gapSm
                                Text {
                                    text: card.name
                                    color: Style.text
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsBody
                                    font.bold: true
                                }
                                PmBadge {
                                    visible: card.isActive
                                    text: "active"
                                    tone: Style.good
                                    filled: true
                                }
                            }
                            Text {
                                text: (card.voice.length > 0 ? card.voice : "no voice") + "  ·  " + card.preferredModel
                                color: Style.textDim
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsSmall
                            }
                            Text {
                                text: card.systemPrompt
                                color: Style.textFaint
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        RowLayout {
                            spacing: Style.gapSm
                            PmButton {
                                text: "Edit"
                                flat: true
                                tone: Style.sectionColor("Personalities")
                                onClicked: root.openEdit(card.index)
                            }
                            PmButton {
                                text: "Duplicate"
                                flat: true
                                tone: Style.info
                                onClicked: {
                                    var src = personalityModel.get(card.index)
                                    var base = src.name + " copy"
                                    var candidate = base
                                    var n = 2
                                    while (personalityModel.indexOfName(candidate) >= 0) {
                                        candidate = base + " " + n
                                        n += 1
                                    }
                                    if (app.createPersonality(candidate)) {
                                        src.name = candidate
                                        app.savePersonality(candidate, JSON.stringify({
                                            name: candidate,
                                            system_prompt: src.systemPrompt,
                                            voice: src.voice,
                                            preferred_model: src.preferredModel,
                                            wake_phrase: src.wakePhrase,
                                            tools: src.tools,
                                            sampling: {
                                                temperature: src.temperature,
                                                top_p: src.topP,
                                                top_k: src.topK,
                                                repeat_penalty: src.repeatPenalty,
                                                max_tokens: src.maxTokens
                                            }
                                        }))
                                        if (src.avatarPath && src.avatarPath.length > 0)
                                            app.setPersonalityAvatar(candidate, src.avatarPath)
                                    }
                                }
                            }
                            PmButton {
                                text: "Delete"
                                flat: true
                                tone: Style.bad
                                enabled: !card.isActive
                                onClicked: {
                                    root.pendingDeleteName = card.name
                                    deleteConfirm.open()
                                }
                            }
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: list.count === 0
                    glyph: "o"
                    iconName: "person"
                    glyphColor: Style.sectionColor("Personalities")
                    title: "No personalities found"
                    hint: "Create one with the New button, or drop a folder under  personalities/<name>/persona.json  and it appears here, hot-loaded."
                    actionVisible: true
                    actionText: "+ New personality"
                    onActionTriggered: root.openNew()
                }
            }
        }
    }

    // ---- editor -----------------------------------------------------------
    // Loaded (not just hidden) so every open gets a fresh instance: field
    // controls bind `text: root.fX` for live two-way sync, and QML permanently
    // breaks that binding the first time the user edits the control — reusing
    // one instance across different personas would then show stale text for
    // any field already touched on a previous edit. Recreating sidesteps that.
    Loader {
        id: editorLoader
        anchors.fill: parent
        active: root.editIndex !== -1
        sourceComponent: Component {
            PersonalityEditor {
                personaName: root.editIndex >= 0 ? personalityModel.get(root.editIndex).name : ""
                onSaved: function(name) { root.closeEditor() }
                onCancelled: root.closeEditor()
            }
        }
    }

    // ---- delete confirm ---------------------------------------------------
    PmDialog {
        id: deleteConfirm
        title: "Delete personality?"
        section: "Personalities"

        Text {
            width: 320
            text: "Delete “" + root.pendingDeleteName + "”? This moves the bundle to  personalities/.trash  — it isn't gone for good, but it disappears from this list."
            color: Style.textDim
            font.family: Style.fontFamily
            font.pixelSize: Style.fsBody
            wrapMode: Text.WordWrap
        }

        footer: [
            PmButton {
                text: "Cancel"
                onClicked: deleteConfirm.close()
            },
            PmButton {
                text: "Delete"
                accent: true
                tone: Style.bad
                onClicked: {
                    app.deletePersonality(root.pendingDeleteName)
                    deleteConfirm.close()
                }
            }
        ]
    }
}
