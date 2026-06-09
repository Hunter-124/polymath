import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

Item {
    id: root
    // app.models() returns a snapshot list of maps; refresh re-reads the table.
    property var modelRows: app.models()
    function reload() { modelRows = app.models() }
    Component.onCompleted: reload()

    // Auto-refresh whenever the registry changes (add / role reassignment / a
    // model load that altered the active row). Guarded so the stub `app` used by
    // the UI render test — which has no such signal — still loads cleanly.
    Connections {
        target: app
        ignoreUnknownSignals: true
        function onModelsChanged() { root.reload() }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: Style.pad; spacing: Style.gap
        Label {
            text: "Model Manager"; color: Style.text
            font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
        }
        Label {
            text: "Add GGUF models and assign roles: Fast (resident), Heavy (on-demand deep work), Vision (image analysis), Embedding (memory). Set GPU layers per the ~8 GB budget."
            color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsBody
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true; spacing: Style.gap
            PmButton { text: "Add GGUF…"; onClicked: app.openModelsFolder() }
            PmComboBox {
                Layout.preferredWidth: 150
                model: ["fast", "heavy", "vision", "embedding"]
            }
            Item { Layout.fillWidth: true }
            PmButton { text: "Refresh"; onClicked: root.reload() }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: Style.radius; color: Style.surface; border.color: Style.borderSoft

            ListView {
                id: list
                anchors.fill: parent; anchors.margins: 8
                clip: true; spacing: 6
                model: root.modelRows

                delegate: Rectangle {
                    id: mrow
                    required property var modelData
                    width: ListView.view ? ListView.view.width : 0
                    height: col.implicitHeight + 18
                    radius: Style.radiusSm; color: Style.surface2
                    border.width: modelData.active === true ? 1 : 0
                    border.color: Style.good

                    RowLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 10
                        // Role accent stripe.
                        Rectangle {
                            width: 4; Layout.fillHeight: true; radius: 2
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
                            Layout.fillWidth: true; spacing: 3
                            RowLayout {
                                Layout.fillWidth: true; spacing: 8
                                Label {
                                    text: mrow.modelData.displayName
                                    color: Style.text; font.family: Style.fontFamily
                                    font.pixelSize: Style.fsBody; font.bold: true
                                }
                                Rectangle {
                                    visible: mrow.modelData.active === true
                                    radius: Style.radiusXs; color: Style.good
                                    implicitWidth: activeLbl.implicitWidth + 12
                                    implicitHeight: activeLbl.implicitHeight + 4
                                    Label {
                                        id: activeLbl; anchors.centerIn: parent
                                        text: "resident"; color: Style.accentText
                                        font.family: Style.fontFamily; font.pixelSize: Style.fsTiny; font.bold: true
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                // Live role assignment: changing this reassigns the
                                // model's role in the `models` table and refreshes.
                                PmComboBox {
                                    id: roleCombo
                                    Layout.preferredWidth: 130
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
                                color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                            }
                            Label {
                                text: mrow.modelData.path
                                color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                                elide: Text.ElideMiddle; Layout.fillWidth: true
                            }
                        }
                    }
                }

                // First-run / no-models guide -> points at fetch-models.
                EmptyState {
                    anchors.fill: parent
                    visible: list.count === 0
                    glyph: "●"
                    glyphColor: Style.accent
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
