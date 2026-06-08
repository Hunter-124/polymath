import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    Component.onCompleted: app.refreshTasks()

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

            // Backed by the C++ TaskModel over the `tasks` table; live updates
            // arrive via EventBus::taskUpdated.
            ListView {
                id: list
                anchors.fill: parent; anchors.margins: 8
                clip: true; spacing: 6
                model: taskModel

                delegate: Rectangle {
                    id: trow
                    required property string type
                    required property string status
                    required property string detail
                    required property int priority
                    width: ListView.view ? ListView.view.width : 0
                    height: col.implicitHeight + 16
                    radius: 8; color: "#1f2335"

                    function statusColor(s) {
                        switch (s) {
                            case "running":  return "#7aa2f7"
                            case "done":     return "#9ece6a"
                            case "error":    return "#f7768e"
                            case "canceled": return "#565f89"
                            default:         return "#e0af68"   // queued
                        }
                    }

                    RowLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 10
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: trow.statusColor(trow.status)
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 4
                        }
                        ColumnLayout {
                            id: col
                            Layout.fillWidth: true; spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: trow.type; color: "#c0caf5"; font.bold: true }
                                Item { Layout.fillWidth: true }
                                Label {
                                    text: trow.status
                                    color: trow.statusColor(trow.status); font.pixelSize: 12
                                }
                            }
                            Label {
                                visible: trow.detail.length > 0
                                text: trow.detail
                                color: "#565f89"; wrapMode: Text.WordWrap; Layout.fillWidth: true
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: "no tasks queued"; color: "#565f89"
                }
            }
        }
    }
}
