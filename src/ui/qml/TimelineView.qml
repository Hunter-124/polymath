import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Timeline — violet glass rows (01 §5.5).
Item {
    Component.onCompleted: app.refreshTimeline()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Memory & Timeline"
            section: "Timeline"
            subtitle: "Searchable daily summaries, transcripts, and detected events."
        }

        PmTextField {
            id: search
            Layout.fillWidth: true
            tone: Style.sectionColor("Timeline")
            placeholderText: "Search memory…"
            text: timelineModel.filter
            onTextChanged: searchTimer.restart()
            Timer {
                id: searchTimer
                interval: 250
                onTriggered: timelineModel.setFilter(search.text)
            }
        }

        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Timeline"

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: Style.gapSm
                clip: true
                spacing: Style.gapSm
                model: timelineModel
                ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Timeline") }

                delegate: GlassCard {
                    id: erow
                    required property string category
                    required property string kind
                    required property string text
                    required property string timeLabel
                    width: ListView.view ? ListView.view.width : 0
                    height: body.implicitHeight + Style.padSm
                    section: "Timeline"
                    radius: Style.radiusSm

                    function categoryColor(c) {
                        switch (c) {
                            case "event":      return Style.magenta
                            case "transcript": return Style.info
                            case "memory":     return Style.good
                            default:           return Style.textFaint
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: Style.gap
                        // Colour rail by category
                        Rectangle {
                            width: 3
                            Layout.fillHeight: true
                            radius: 2
                            color: erow.categoryColor(erow.category)
                        }
                        ColumnLayout {
                            Layout.alignment: Qt.AlignTop
                            spacing: 2
                            Layout.preferredWidth: 92
                            Label {
                                text: erow.timeLabel
                                color: Style.textFaint
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny
                            }
                            PmBadge {
                                text: erow.kind
                                tone: erow.categoryColor(erow.category)
                            }
                        }
                        Label {
                            id: body
                            Layout.fillWidth: true
                            text: erow.text
                            color: Style.text
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsBody
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: list.count === 0
                    glyph: search.text.length > 0 ? "?" : "o"
                    iconName: "clock"
                    glyphColor: Style.sectionColor("Timeline")
                    title: search.text.length > 0 ? "No matches" : "Your timeline is empty"
                    hint: search.text.length > 0
                        ? "Nothing in memory matches \"" + search.text + "\". Try a broader term."
                        : "As Polymath listens and watches, detected events, transcripts and daily summaries collect here — fully searchable."
                }
            }
        }
    }
}
