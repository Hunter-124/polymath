import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

Item {
    Component.onCompleted: app.refreshTasks()

    ColumnLayout {
        anchors.fill: parent; anchors.margins: Style.pad; spacing: Style.gap
        Label {
            text: "Deep-Work Task Queue"; color: Style.text
            font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
        }
        Label {
            text: "Long/important jobs (lab reports, research, daily summaries) run here when the machine is idle and the heavy model can be loaded."
            color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsBody
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: Style.radius; color: Style.surface; border.color: Style.borderSoft

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
                    height: col.implicitHeight + 18
                    radius: Style.radiusSm; color: Style.surface2

                    function statusColor(s) {
                        switch (s) {
                            case "running":  return Style.accent
                            case "done":     return Style.good
                            case "error":    return Style.bad
                            case "canceled": return Style.textFaint
                            default:         return Style.warn   // queued
                        }
                    }

                    RowLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 12
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            color: trow.statusColor(trow.status)
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 4
                            // pulse while running
                            SequentialAnimation on opacity {
                                running: trow.status === "running"; loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 650 }
                                NumberAnimation { to: 1.0; duration: 650 }
                            }
                        }
                        ColumnLayout {
                            id: col
                            Layout.fillWidth: true; spacing: 3
                            RowLayout {
                                Layout.fillWidth: true; spacing: 8
                                Label {
                                    text: trow.type; color: Style.text; font.family: Style.fontFamily
                                    font.pixelSize: Style.fsBody; font.bold: true
                                }
                                Item { Layout.fillWidth: true }
                                Rectangle {
                                    radius: Style.radiusXs
                                    color: Qt.rgba(trow.statusColor(trow.status).r,
                                                   trow.statusColor(trow.status).g,
                                                   trow.statusColor(trow.status).b, 0.16)
                                    implicitWidth: stLbl.implicitWidth + 14
                                    implicitHeight: stLbl.implicitHeight + 5
                                    Label {
                                        id: stLbl; anchors.centerIn: parent
                                        text: trow.status; color: trow.statusColor(trow.status)
                                        font.family: Style.fontFamily; font.pixelSize: Style.fsTiny; font.bold: true
                                    }
                                }
                            }
                            Label {
                                visible: trow.detail.length > 0
                                text: trow.detail; color: Style.textDim
                                font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                wrapMode: Text.WordWrap; Layout.fillWidth: true
                            }
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: list.count === 0
                    glyph: "●"
                    title: "No deep-work tasks queued"
                    hint: "Ask the assistant for something heavy — \"write a lab report\", \"research X\" — and it queues here, running when the machine is idle."
                }
            }
        }
    }
}
