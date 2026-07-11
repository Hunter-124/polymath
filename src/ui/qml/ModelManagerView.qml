import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Models — indigo glass rows (01 §5.8).
Item {
    id: root
    // app.models() returns a snapshot list of maps; refresh re-reads the table.
    property var modelRows: app.models()
    function reload() { modelRows = app.models() }
    Component.onCompleted: reload()

    // Auto-refresh whenever the registry changes. Guarded so the stub `app`
    // used by the UI render test — which has no such signal — still loads.
    Connections {
        target: app
        ignoreUnknownSignals: true
        function onModelsChanged() { root.reload() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Model Manager"
            section: "Models"
            subtitle: "Add GGUF models and assign roles: Fast (resident), Heavy (on-demand deep work), Vision (image analysis), Embedding (memory). Set GPU layers per the ~8 GB budget."
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gap
            PmComboBox {
                id: roleCombo
                Layout.preferredWidth: 150
                tone: Style.sectionColor("Models")
                model: ["fast", "heavy", "vision", "embedding"]
            }
            PmButton {
                text: "Add GGUF…"
                onClicked: {
                    // Prefer native picker (C++ QFileDialog); fall back to folder open.
                    if (typeof app.pickAndAddModel === "function")
                        app.pickAndAddModel(roleCombo.currentText || "fast")
                    else
                        app.openModelsFolder()
                    root.reload()
                }
            }
            PmButton {
                text: "Open folder"
                onClicked: app.openModelsFolder()
            }
            Item { Layout.fillWidth: true }
            PmButton {
                text: "Refresh"
                onClicked: root.reload()
            }
        }

        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Models"

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: Style.gapSm
                clip: true
                spacing: Style.gapSm
                model: root.modelRows
                ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Models") }

                delegate: GlassCard {
                    id: mrow
                    required property var modelData
                    width: ListView.view ? ListView.view.width : 0
                    height: col.implicitHeight + 18
                    section: "Models"
                    radius: Style.radiusSm
                    // Active / resident emphasis via border glow
                    // (content lives in GlassPanel's contentHolder — use mrow.radius, not parent.radius)
                    Rectangle {
                        anchors.fill: parent
                        radius: mrow.radius
                        color: "transparent"
                        border.width: modelData.active === true ? 1 : 0
                        border.color: Style.good
                        z: 3
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Style.gapSm
                        spacing: Style.gapSm
                        z: 2
                        // Role accent stripe
                        Rectangle {
                            width: 4
                            Layout.fillHeight: true
                            radius: 2
                            color: {
                                switch (mrow.modelData.role) {
                                    case "fast":      return Style.accent
                                    case "heavy":     return Style.magenta
                                    case "vision":    return Style.info
                                    case "embedding": return Style.good
                                    default:          return Style.textFaint
                                }
                            }
                        }
                        ColumnLayout {
                            id: col
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Style.gapSm
                                Label {
                                    text: mrow.modelData.displayName
                                    color: Style.text
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsBody
                                    font.bold: true
                                }
                                PmBadge {
                                    visible: mrow.modelData.active === true
                                    text: "resident"
                                    tone: Style.good
                                    filled: true
                                }
                                Item { Layout.fillWidth: true }
                                PmComboBox {
                                    id: roleCombo
                                    Layout.preferredWidth: 130
                                    tone: Style.sectionColor("Models")
                                    model: ["fast", "heavy", "vision", "embedding"]
                                    currentIndex: Math.max(0, model.indexOf(mrow.modelData.role))
                                    onActivated: {
                                        const r = model[currentIndex]
                                        if (r !== mrow.modelData.role) {
                                            app.setModelRole(mrow.modelData.id, r)
                                            root.reload()
                                        }
                                    }
                                }
                            }
                            Label {
                                text: "ctx " + mrow.modelData.nCtx + "   ·   gpu layers " + mrow.modelData.nGpuLayers
                                color: Style.textDim
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsSmall
                            }
                            Label {
                                text: mrow.modelData.path
                                color: Style.textFaint
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: list.count === 0
                    glyph: "*"
                    iconName: "chip"
                    glyphColor: Style.sectionColor("Models")
                    title: "No models registered yet"
                    hint: "Polymath needs at least a Fast model to think. Run  scripts/fetch-models.ps1  to download the default local set (Gemma 3n, embeddings, voices), or drop a .gguf into data/models/ and click Refresh."
                    actionVisible: true
                    actionText: "Open models folder"
                    onActionTriggered: app.openModelsFolder()
                }
            }
        }
    }
}
