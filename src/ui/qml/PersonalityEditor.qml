import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// PersonalityEditor — create/edit page for a persona bundle (overhaul2 E2).
// Host (PersonalitiesView) toggles visibility and sets `personaName`; "" means
// create-new. Existing personas are read from the `personalityModel` context
// property; writes round-trip through AppController's pass-throughs to
// PersonalityManager's write API (createPersonality/savePersonality/
// setPersonalityAvatar). Name is fixed once the bundle exists on disk — this
// editor doesn't attempt a rename (use Duplicate + Delete for that).
Item {
    id: root
    property string personaName: ""
    readonly property bool isNew: personaName.length === 0

    signal saved(string name)
    signal cancelled()

    // ---- edit-local field state ---------------------------------------
    property string fName: ""
    property string fSystemPrompt: ""
    property string fVoice: ""
    property string fPreferredModel: "fast"
    property string fWakePhrase: ""
    property var    fTools: []
    property string fAvatarPath: ""
    property real   fTemperature: 0.7
    property real   fTopP: 0.9
    property int    fTopK: 40
    property real   fRepeatPenalty: 1.1
    property int    fMaxTokens: 1024

    property bool dirty: false
    // True once the bundle actually exists on disk (either we loaded an
    // existing one, or Save has already run createPersonality() once this
    // session) — gates renaming and the avatar importer.
    property bool created: !isNew
    property string avatarSourcePath: ""

    property var voiceIdList: []
    property var voiceLabelList: []

    readonly property var toolNames: (typeof app !== "undefined" && app.availableToolNames)
        ? (app.availableToolNames() || []) : []
    readonly property var modelChoices: (typeof app !== "undefined" && app.availableModels)
        ? (app.availableModels() || ["fast", "heavy"]) : ["fast", "heavy"]

    function refreshVoiceChoices() {
        var list = (typeof settings !== "undefined" && settings.ttsVoices)
            ? (settings.ttsVoices() || []) : []
        var ids = [], labels = []
        for (var i = 0; i < list.length; ++i) {
            ids.push(list[i].id)
            labels.push(list[i].label)
        }
        root.voiceIdList = ids
        root.voiceLabelList = labels
    }

    function loadFrom(name) {
        root.created = !root.isNew
        if (root.isNew) {
            fName = ""; fSystemPrompt = ""; fVoice = ""; fPreferredModel = "fast"
            fWakePhrase = ""; fTools = []; fAvatarPath = ""
            fTemperature = 0.7; fTopP = 0.9; fTopK = 40; fRepeatPenalty = 1.1; fMaxTokens = 1024
        } else if (typeof personalityModel !== "undefined") {
            var row = personalityModel.indexOfName(name)
            if (row >= 0) {
                var d = personalityModel.get(row)
                fName = d.name || ""
                fSystemPrompt = d.systemPrompt || ""
                fVoice = d.voice || ""
                fPreferredModel = d.preferredModel || "fast"
                fWakePhrase = d.wakePhrase || ""
                fTools = d.tools || []
                fAvatarPath = d.avatarPath || ""
                fTemperature = d.temperature !== undefined ? d.temperature : 0.7
                fTopP = d.topP !== undefined ? d.topP : 0.9
                fTopK = d.topK !== undefined ? d.topK : 40
                fRepeatPenalty = d.repeatPenalty !== undefined ? d.repeatPenalty : 1.1
                fMaxTokens = d.maxTokens !== undefined ? d.maxTokens : 1024
            }
        }
        avatarSourcePath = ""
        statusLabel.text = ""
        root.dirty = false
    }

    onPersonaNameChanged: root.loadFrom(root.personaName)
    onVisibleChanged: if (root.visible) root.loadFrom(root.personaName)
    Component.onCompleted: { root.refreshVoiceChoices(); root.loadFrom(root.personaName) }

    function markDirty() { root.dirty = true }

    function toolChecked(t) { return root.fTools.indexOf(t) >= 0 }
    function toggleTool(t, on) {
        var arr = root.fTools.slice()
        var i = arr.indexOf(t)
        if (on && i < 0) arr.push(t)
        else if (!on && i >= 0) arr.splice(i, 1)
        root.fTools = arr
        root.markDirty()
    }

    function buildPersonaObject() {
        return {
            name: root.fName,
            system_prompt: root.fSystemPrompt,
            voice: root.fVoice,
            preferred_model: root.fPreferredModel,
            wake_phrase: root.fWakePhrase,
            tools: root.fTools,
            sampling: {
                temperature: root.fTemperature,
                top_p: root.fTopP,
                top_k: Math.round(root.fTopK),
                repeat_penalty: root.fRepeatPenalty,
                max_tokens: Math.round(root.fMaxTokens)
            }
        }
    }

    function doSave() {
        var nm = String(root.fName).trim()
        if (nm.length === 0) return
        if (!root.created) {
            if (!app.createPersonality(nm)) {
                statusLabel.text = "Could not create — that name may already be taken."
                return
            }
            root.created = true
        }
        var ok = app.savePersonality(nm, JSON.stringify(root.buildPersonaObject()))
        if (!ok) {
            statusLabel.text = "Save failed — see the log for details."
            return
        }
        if (root.avatarSourcePath.length > 0) {
            app.setPersonalityAvatar(nm, root.avatarSourcePath)
            root.avatarSourcePath = ""
        }
        root.dirty = false
        statusLabel.text = ""
        root.saved(nm)
    }

    function requestCancel() {
        if (root.dirty) discardConfirm.open()
        else root.cancelled()
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
                title: root.created ? ("Edit — " + root.fName) : "New Personality"
                section: "Personalities"
                subtitle: "Full system prompt, voice, model, tool allow-list and sampling — saved straight to persona.json."
            }
            PmButton {
                text: "Cancel"
                flat: true
                onClicked: root.requestCancel()
            }
            PmButton {
                text: "Save"
                accent: true
                tone: Style.sectionColor("Personalities")
                enabled: root.fName.trim().length > 0
                onClicked: root.doSave()
            }
        }

        Text {
            id: statusLabel
            Layout.fillWidth: true
            visible: text.length > 0
            color: Style.bad
            font.family: Style.fontFamily
            font.pixelSize: Style.fsSmall
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Personalities") }

            ColumnLayout {
                id: content
                width: parent.width
                spacing: Style.gapLg

                // ---------- Identity ----------
                GlassCard {
                    Layout.fillWidth: true
                    section: "Personalities"
                    implicitHeight: identityCol.implicitHeight + Style.padSm * 2

                    ColumnLayout {
                        id: identityCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Style.padSm
                        spacing: Style.gap

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Style.gapLg

                            Rectangle {
                                Layout.preferredWidth: 88
                                Layout.preferredHeight: 88
                                Layout.alignment: Qt.AlignTop
                                radius: 44
                                color: Style.tint(Style.sectionColor("Personalities"), 0.16)
                                border.width: 1
                                border.color: Style.tint(Style.sectionColor("Personalities"), 0.45)

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    visible: root.fAvatarPath.length > 0
                                    source: root.fAvatarPath.length > 0 ? "file:///" + root.fAvatarPath : ""
                                    fillMode: Image.PreserveAspectCrop
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: root.fAvatarPath.length === 0
                                    text: root.fName.length > 0 ? root.fName.charAt(0).toUpperCase() : "?"
                                    color: Style.sectionColor("Personalities")
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsTitle
                                    font.bold: true
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Style.gapSm

                                Text { text: "Name"; color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall }
                                PmTextField {
                                    Layout.fillWidth: true
                                    text: root.fName
                                    enabled: !root.created
                                    placeholderText: "e.g. Ada Lovelace"
                                    tone: Style.sectionColor("Personalities")
                                    onTextEdited: { root.fName = text; root.markDirty() }
                                }
                                Text {
                                    visible: root.created
                                    text: "Name is fixed once the bundle is created — Duplicate makes a copy under a new name."
                                    color: Style.textFaint
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsTiny
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                Text { text: "Avatar image path"; color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Style.gapSm
                                    PmTextField {
                                        id: avatarField
                                        Layout.fillWidth: true
                                        placeholderText: "C:\\path\\to\\image.png"
                                        tone: Style.sectionColor("Personalities")
                                        onTextEdited: root.avatarSourcePath = text
                                    }
                                    PmButton {
                                        text: "Import"
                                        flat: true
                                        enabled: root.created && avatarField.text.length > 0
                                        onClicked: {
                                            if (app.setPersonalityAvatar(root.fName, avatarField.text)) {
                                                avatarField.text = ""
                                                root.avatarSourcePath = ""
                                                root.loadFrom(root.fName)
                                            } else {
                                                statusLabel.text = "Could not import that image — png/jpg/jpeg only, and it must exist."
                                            }
                                        }
                                    }
                                }
                                Text {
                                    text: root.created
                                        ? "Paste an absolute path to a png/jpg and click Import — it copies into the bundle."
                                        : "Save once to create the bundle before adding an avatar."
                                    color: Style.textFaint
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsTiny
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }

                // ---------- System prompt ----------
                GlassCard {
                    Layout.fillWidth: true
                    section: "Personalities"
                    implicitHeight: promptCol.implicitHeight + Style.padSm * 2

                    ColumnLayout {
                        id: promptCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Style.padSm
                        spacing: Style.gapSm

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "System prompt"
                                color: Style.textDim
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsSmall
                                Layout.fillWidth: true
                            }
                            Text {
                                text: root.fSystemPrompt.length + " chars"
                                color: Style.textFaint
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 220
                            radius: Style.radiusSm
                            color: Qt.rgba(1, 1, 1, 0.04)
                            border.width: promptArea.activeFocus ? 2 : 1
                            border.color: promptArea.activeFocus ? Style.sectionColor("Personalities") : Style.glassBorder

                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: 2
                                clip: true
                                TextArea {
                                    id: promptArea
                                    text: root.fSystemPrompt
                                    wrapMode: Text.WordWrap
                                    selectByMouse: true
                                    color: Style.text
                                    selectionColor: Style.sectionColor("Personalities")
                                    font.family: "Consolas, Cascadia Mono, monospace"
                                    font.pixelSize: Style.fsSmall
                                    leftPadding: 8; rightPadding: 8; topPadding: 6; bottomPadding: 6
                                    background: Item {}
                                    onTextChanged: { root.fSystemPrompt = text; root.markDirty() }
                                }
                            }
                        }
                    }
                }

                // ---------- Voice / model / wake phrase ----------
                GlassCard {
                    Layout.fillWidth: true
                    section: "Personalities"
                    implicitHeight: voiceCol.implicitHeight + Style.padSm * 2

                    ColumnLayout {
                        id: voiceCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Style.padSm
                        spacing: Style.gap

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Style.gapLg

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "Voice"; color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall }
                                PmComboBox {
                                    id: voiceCombo
                                    Layout.fillWidth: true
                                    visible: root.voiceIdList.length > 0
                                    tone: Style.sectionColor("Personalities")
                                    model: root.voiceLabelList
                                    currentIndex: Math.max(0, root.voiceIdList.indexOf(root.fVoice))
                                    onActivated: {
                                        root.fVoice = root.voiceIdList[currentIndex] || ""
                                        root.markDirty()
                                    }
                                }
                                PmTextField {
                                    Layout.fillWidth: true
                                    visible: root.voiceIdList.length === 0
                                    text: root.fVoice
                                    placeholderText: "e.g. af_heart"
                                    tone: Style.sectionColor("Personalities")
                                    onTextEdited: { root.fVoice = text; root.markDirty() }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "Preferred model"; color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall }
                                PmComboBox {
                                    Layout.fillWidth: true
                                    tone: Style.sectionColor("Personalities")
                                    model: root.modelChoices
                                    currentIndex: Math.max(0, root.modelChoices.indexOf(root.fPreferredModel))
                                    onActivated: {
                                        root.fPreferredModel = root.modelChoices[currentIndex]
                                        root.markDirty()
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "Wake phrase"; color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall }
                                PmTextField {
                                    Layout.fillWidth: true
                                    text: root.fWakePhrase
                                    placeholderText: "optional"
                                    tone: Style.sectionColor("Personalities")
                                    onTextEdited: { root.fWakePhrase = text; root.markDirty() }
                                }
                            }
                        }
                    }
                }

                // ---------- Tools ----------
                GlassCard {
                    Layout.fillWidth: true
                    section: "Personalities"
                    implicitHeight: toolsCol.implicitHeight + Style.padSm * 2

                    ColumnLayout {
                        id: toolsCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Style.padSm
                        spacing: Style.gapSm

                        Text {
                            text: "Tool allow-list  (" + root.fTools.length + " of " + root.toolNames.length
                                  + " selected — none selected means all tools)"
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Style.gapSm
                            Repeater {
                                model: root.toolNames
                                delegate: PmCheckBox {
                                    required property string modelData
                                    text: modelData
                                    tone: Style.sectionColor("Personalities")
                                    checked: root.toolChecked(modelData)
                                    onToggled: root.toggleTool(modelData, checked)
                                }
                            }
                        }
                        Text {
                            visible: root.toolNames.length === 0
                            text: "No tools registered yet."
                            color: Style.textFaint
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                        }
                    }
                }

                // ---------- Sampling ----------
                GlassCard {
                    Layout.fillWidth: true
                    section: "Personalities"
                    implicitHeight: samplingCol.implicitHeight + Style.padSm * 2

                    ColumnLayout {
                        id: samplingCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Style.padSm
                        spacing: Style.gap

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "Sampling"
                                color: Style.text
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsBody
                                font.bold: true
                                Layout.fillWidth: true
                            }
                            PmButton {
                                text: "Reset to defaults"
                                flat: true
                                onClicked: {
                                    root.fTemperature = 0.7; root.fTopP = 0.9; root.fTopK = 40
                                    root.fRepeatPenalty = 1.1; root.fMaxTokens = 1024
                                    root.markDirty()
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: "Temperature  " + root.fTemperature.toFixed(2); color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall }
                            PmSlider {
                                Layout.fillWidth: true
                                tone: Style.sectionColor("Personalities")
                                from: 0; to: 2
                                value: root.fTemperature
                                onMoved: { root.fTemperature = value; root.markDirty() }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: "Top P  " + root.fTopP.toFixed(2); color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall }
                            PmSlider {
                                Layout.fillWidth: true
                                tone: Style.sectionColor("Personalities")
                                from: 0; to: 1
                                value: root.fTopP
                                onMoved: { root.fTopP = value; root.markDirty() }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: "Top K  " + Math.round(root.fTopK); color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall }
                            PmSlider {
                                Layout.fillWidth: true
                                tone: Style.sectionColor("Personalities")
                                from: 1; to: 100
                                value: root.fTopK
                                onMoved: { root.fTopK = Math.round(value); root.markDirty() }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: "Repeat penalty  " + root.fRepeatPenalty.toFixed(2); color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall }
                            PmSlider {
                                Layout.fillWidth: true
                                tone: Style.sectionColor("Personalities")
                                from: 1; to: 2
                                value: root.fRepeatPenalty
                                onMoved: { root.fRepeatPenalty = value; root.markDirty() }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: "Max tokens  " + Math.round(root.fMaxTokens); color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall }
                            PmSlider {
                                Layout.fillWidth: true
                                tone: Style.sectionColor("Personalities")
                                from: 64; to: 4096
                                value: root.fMaxTokens
                                onMoved: { root.fMaxTokens = Math.round(value); root.markDirty() }
                            }
                        }
                    }
                }

                Item { Layout.preferredHeight: Style.pad }
            }
        }
    }

    // ---- dirty-state guard on Cancel --------------------------------------
    PmDialog {
        id: discardConfirm
        title: "Discard changes?"
        section: "Personalities"

        Text {
            width: 320
            text: "You have unsaved edits to this personality. Discard them?"
            color: Style.textDim
            font.family: Style.fontFamily
            font.pixelSize: Style.fsBody
            wrapMode: Text.WordWrap
        }

        footer: [
            PmButton {
                text: "Keep editing"
                flat: true
                onClicked: discardConfirm.close()
            },
            PmButton {
                text: "Discard"
                accent: true
                tone: Style.bad
                onClicked: {
                    discardConfirm.close()
                    root.dirty = false
                    root.cancelled()
                }
            }
        ]
    }
}
