import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 12
        Label { text: "Memory & Timeline"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }
        Label {
            text: "Searchable daily summaries, transcripts, and detected events."
            color: "#565f89"; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        TextField { Layout.fillWidth: true; placeholderText: "Search memory…" }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: 10; color: "#171a21"; border.color: "#24283b"
            Label { anchors.centerIn: parent; text: "timeline appears here"; color: "#565f89" }
        }
    }
}
