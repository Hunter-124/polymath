import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

Item {
    id: root
    property var people: app.personalities()
    Component.onCompleted: people = app.personalities()

    Connections {
        target: app
        function onActivePersonalityChanged() { root.people = app.personalities() }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: Style.pad; spacing: Style.gap
        Label {
            text: "Personalities"; color: Style.text
            font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
        }
        Label {
            text: "Modular historical minds. Drop a folder into  personalities/<name>/persona.json  to add one."
            color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsBody
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: Style.radius; color: Style.surface; border.color: Style.borderSoft

            ListView {
                id: list
                anchors.fill: parent; anchors.margins: 8; clip: true; spacing: 6
                model: root.people
                delegate: PmItemDelegate {
                    id: pd
                    required property string modelData
                    width: ListView.view ? ListView.view.width : 0
                    text: pd.modelData
                    highlighted: pd.modelData === app.activePersonality
                    onClicked: app.setPersonality(pd.modelData)

                    Label {
                        anchors.right: parent.right; anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        visible: pd.modelData === app.activePersonality
                        text: "● active"; color: Style.good
                        font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: list.count === 0
                    glyph: "○"
                    title: "No personalities found"
                    hint: "Add a persona bundle under  personalities/<name>/persona.json  and it appears here, hot-loaded."
                }
            }
        }
    }
}
