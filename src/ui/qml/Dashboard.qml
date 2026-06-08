import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Label { text: "Dashboard"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }

        GridLayout {
            columns: 3; columnSpacing: 16; rowSpacing: 16
            Layout.fillWidth: true

            component Card: Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                radius: 10; color: "#171a21"; border.color: "#24283b"
            }

            Card {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16
                    Label { text: "Assistant"; color: "#7aa2f7"; font.bold: true }
                    Label { text: app.listening ? "Listening" : "Idle"; color: "#c0caf5"; font.pixelSize: 22 }
                    Label { text: app.activePersonality; color: "#565f89" }
                }
            }
            Card {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16
                    Label { text: "Model"; color: "#7aa2f7"; font.bold: true }
                    Label { text: app.modelStatus; color: "#c0caf5"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
            Card {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16
                    Label { text: "Today"; color: "#7aa2f7"; font.bold: true }
                    Label { text: "Reminders & suggestions appear here"; color: "#565f89"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: 10; color: "#171a21"; border.color: "#24283b"
            Label {
                anchors.centerIn: parent
                text: "Find something:  ask Chat \"where did I leave my keys?\""
                color: "#565f89"
            }
        }
    }
}
