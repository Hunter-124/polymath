import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    // app.models() returns a snapshot list of maps; refresh re-reads the table.
    property var modelRows: app.models()
    function reload() { modelRows = app.models() }
    Component.onCompleted: reload()

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 12
        Label { text: "Model Manager"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }
        Label {
            text: "Add GGUF models and assign roles: Fast (resident), Heavy (on-demand deep work),\nVision (image analysis), Embedding (memory). Set GPU layers per the ~8 GB budget."
            color: "#565f89"; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Add GGUF…" }
            ComboBox { model: ["fast", "heavy", "vision", "embedding"] }
            Item { Layout.fillWidth: true }
            Button { text: "Refresh"; onClicked: root.reload() }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: 10; color: "#171a21"; border.color: "#24283b"

            // Lists the rows of the `models` table (via app.models()).
            ListView {
                id: list
                anchors.fill: parent; anchors.margins: 8
                clip: true; spacing: 6
                model: root.modelRows

                delegate: Rectangle {
                    id: mrow
                    required property var modelData
                    width: ListView.view ? ListView.view.width : 0
                    height: col.implicitHeight + 16
                    radius: 8; color: "#1f2335"

                    RowLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 10
                        ColumnLayout {
                            id: col
                            Layout.fillWidth: true; spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: mrow.modelData.displayName
                                    color: "#c0caf5"; font.bold: true
                                }
                                Rectangle {
                                    visible: mrow.modelData.active === true
                                    radius: 4; color: "#9ece6a"
                                    implicitWidth: activeLbl.implicitWidth + 10
                                    implicitHeight: activeLbl.implicitHeight + 4
                                    Label {
                                        id: activeLbl; anchors.centerIn: parent
                                        text: "active"; color: "#1a1b26"; font.pixelSize: 10; font.bold: true
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                Label {
                                    text: mrow.modelData.role
                                    color: "#7aa2f7"; font.pixelSize: 12
                                }
                            }
                            Label {
                                text: "ctx " + mrow.modelData.nCtx + "  ·  gpu layers " + mrow.modelData.nGpuLayers
                                color: "#565f89"; font.pixelSize: 11
                            }
                            Label {
                                text: mrow.modelData.path
                                color: "#414868"; font.pixelSize: 10
                                elide: Text.ElideMiddle; Layout.fillWidth: true
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: "registered models appear here"; color: "#565f89"
                }
            }
        }
    }
}
