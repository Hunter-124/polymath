import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 12
        Label { text: "Cameras"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }

        RowLayout {
            Layout.fillWidth: true
            TextField { id: q; Layout.fillWidth: true; placeholderText: "Find an object… (e.g. my keys)" ; onAccepted: app.findObject(q.text) }
            Button { text: "Find"; onClicked: app.findObject(q.text) }
        }

        // Live ESP32-CAM tiles are streamed in by the Wave-1/3 vision + UI work
        // (frames arrive on EventBus::frameReady -> a QQuickImageProvider).
        GridLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            columns: 2; columnSpacing: 12; rowSpacing: 12
            Repeater {
                model: 2
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 10; color: "#171a21"; border.color: "#24283b"
                    Label { anchors.centerIn: parent; text: "camera tile"; color: "#565f89" }
                }
            }
        }

        Connections {
            target: app
            function onFindObjectAnswered(query, answer) {
                ans.text = query + " → " + answer
            }
        }
        Label { id: ans; color: "#9ece6a"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    }
}
