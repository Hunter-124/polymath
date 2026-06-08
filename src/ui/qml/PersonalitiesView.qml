import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    // app.personalities() is a snapshot; re-pull when the active one changes
    // (a switch can accompany a hot-added bundle).
    property var people: app.personalities()
    Component.onCompleted: people = app.personalities()

    Connections {
        target: app
        function onActivePersonalityChanged() { root.people = app.personalities() }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 12
        Label { text: "Personalities"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }
        Label {
            text: "Modular historical minds. Drop a folder into personalities/<name>/persona.json to add one."
            color: "#565f89"; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        ListView {
            id: list
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 6
            model: root.people
            delegate: ItemDelegate {
                id: pd
                required property string modelData
                width: ListView.view ? ListView.view.width : 0
                text: pd.modelData
                highlighted: pd.modelData === app.activePersonality
                onClicked: app.setPersonality(pd.modelData)

                // Trailing "active" marker.
                Label {
                    anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    visible: pd.modelData === app.activePersonality
                    text: "● active"; color: "#9ece6a"; font.pixelSize: 12
                }
            }

            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "No personalities found."
                color: "#565f89"
            }
        }
    }
}
