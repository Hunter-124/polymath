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
                                Rectangle {
                                    radius: Style.radiusXs; color: Style.surface3
                                    implicitWidth: roleLbl.implicitWidth + 14
                                    implicitHeight: roleLbl.implicitHeight + 5
                                    Label {
                                        id: roleLbl; anchors.centerIn: parent
                                        text: mrow.modelData.role; color: Style.accent
                                        font.family: Style.fontFamily; font.pixelSize: Style.fsTiny; font.bold: true
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
