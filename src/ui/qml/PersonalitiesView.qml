import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 12
        Label { text: "Personalities"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }
        Label {
            text: "Modular historical minds. Drop a folder into personalities/<name>/persona.json to add one."
            color: "#565f89"; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 6
            model: app.personalities()
            delegate: ItemDelegate {
                required property string modelData
                width: ListView.view.width
                text: modelData
                highlighted: modelData === app.activePersonality
                onClicked: app.setPersonality(modelData)
            }
        }
    }
}
