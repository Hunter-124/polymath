import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 12
        Label { text: "Deep-Work Task Queue"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }
        Label {
            text: "Long/important jobs (lab reports, research, daily summaries) run here when the\nmachine is idle and the heavy model can be loaded."
            color: "#565f89"; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: 10; color: "#171a21"; border.color: "#24283b"
            // Wave-3 UI agent binds this to a C++ model over the `tasks` table.
            Label { anchors.centerIn: parent; text: "no tasks queued"; color: "#565f89" }
        }
    }
}
