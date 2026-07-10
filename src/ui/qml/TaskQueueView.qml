import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Tasks — amber glass rows (01 §5.4).
Item {
    Component.onCompleted: app.refreshTasks()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Deep-Work Task Queue"
            section: "Tasks"
            subtitle: "Long/important jobs (lab reports, research, daily summaries) run here when the machine is idle and the heavy model can be loaded."
        }

        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Tasks"

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: Style.gapSm
                clip: true
                spacing: Style.gapSm
                model: taskModel
                ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Tasks") }

                delegate: GlassCard {
                    id: trow
                    required property string type
                    required property string status
                    required property string detail
                    required property int priority
                    width: ListView.view ? ListView.view.width : 0
                    height: col.implicitHeight + 18
                    section: "Tasks"
                    radius: Style.radiusSm

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
                        anchors.fill: parent
                        anchors.margins: Style.gapSm
                        spacing: Style.gap
                        PmStatusDot {
                            tone: trow.statusColor(trow.status)
                            pulsing: trow.status === "running"
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 4
                        }
                        ColumnLayout {
                            id: col
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Style.gapSm
                                Label {
                                    text: trow.type
                                    color: Style.text
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsBody
                                    font.bold: true
                                }
                                Item { Layout.fillWidth: true }
                                PmBadge {
                                    text: trow.status
                                    tone: trow.statusColor(trow.status)
                                }
                            }
                            Label {
                                visible: trow.detail.length > 0
                                text: trow.detail
                                color: Style.textDim
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsSmall
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: list.count === 0
                    glyph: "*"
                    iconName: "tasks"
                    glyphColor: Style.sectionColor("Tasks")
                    title: "No deep-work tasks queued"
                    hint: "Ask the assistant for something heavy — \"write a lab report\", \"research X\" — and it queues here, running when the machine is idle."
                }
            }
        }
    }
}
