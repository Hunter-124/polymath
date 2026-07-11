import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Tasks — amber glass rows (01 §5.4). Scheduler v2 (overhaul2 D1) adds a
// second "Scheduled" section below the deep-work queue: timed/recurring
// agent goals (scheduled_goals table via ScheduledGoalsModel).
Item {
    Component.onCompleted: {
        app.refreshTasks()
        app.refreshSchedules()
    }

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
            Layout.preferredHeight: parent.height * 0.5
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

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Scheduled"
            section: "Tasks"
            subtitle: "Timed and recurring agent goals — \"every morning at 8 give me a briefing\" lands here and fires through the same agent harness."
        }

        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Tasks"

            ListView {
                id: schedList
                anchors.fill: parent
                anchors.margins: Style.gapSm
                clip: true
                spacing: Style.gapSm
                model: scheduledGoalsModel
                ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Tasks") }

                delegate: GlassCard {
                    id: srow
                    required property int index
                    required property string title
                    required property string kind
                    required property string spec
                    required property double nextFire
                    required property bool enabled
                    required property string deliver
                    width: ListView.view ? ListView.view.width : 0
                    height: scol.implicitHeight + 18
                    section: "Tasks"
                    radius: Style.radiusSm

                    function kindLabel(k, spec_) {
                        switch (k) {
                            case "at":    return "once"
                            case "every": return "every " + spec_ + "s"
                            case "rrule": return spec_
                            default:      return k
                        }
                    }
                    function nextFireLabel(ts) {
                        if (!ts || ts <= 0) return "not scheduled"
                        return new Date(ts * 1000).toLocaleString(Qt.locale(), "ddd MMM d, HH:mm")
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Style.gapSm
                        spacing: Style.gap
                        PmStatusDot {
                            tone: srow.enabled ? Style.sectionColor("Tasks") : Style.textFaint
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 4
                        }
                        ColumnLayout {
                            id: scol
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Style.gapSm
                                Label {
                                    text: srow.title
                                    color: srow.enabled ? Style.text : Style.textFaint
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsBody
                                    font.bold: true
                                }
                                Item { Layout.fillWidth: true }
                                PmBadge {
                                    text: srow.kindLabel(srow.kind, srow.spec)
                                    tone: Style.sectionColor("Tasks")
                                }
                                PmBadge {
                                    visible: srow.deliver === "voice"
                                    text: "voice"
                                    tone: Style.info
                                }
                            }
                            Label {
                                text: "Next: " + srow.nextFireLabel(srow.nextFire)
                                color: Style.textDim
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsSmall
                            }
                        }
                        PmCheckBox {
                            checked: srow.enabled
                            onToggled: scheduledGoalsModel.setEnabled(srow.index, checked)
                        }
                        PmToolButton {
                            iconName: "trash"
                            onClicked: scheduledGoalsModel.removeItem(srow.index)
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: schedList.count === 0
                    glyph: "*"
                    iconName: "tasks"
                    glyphColor: Style.sectionColor("Tasks")
                    title: "Nothing scheduled"
                    hint: "Ask the assistant to schedule something — \"remind me every morning at 8 with a briefing\" — and it lands here."
                }
            }
        }
    }
}
