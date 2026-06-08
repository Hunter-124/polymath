import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
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
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: 10; color: "#171a21"; border.color: "#24283b"
            // Wave-3 UI agent binds this to a C++ model over the `models` table.
            Label { anchors.centerIn: parent; text: "registered models appear here"; color: "#565f89" }
        }
    }
}
