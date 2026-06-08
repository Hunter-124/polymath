import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    Component.onCompleted: app.refreshTimeline()

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 12
        Label { text: "Memory & Timeline"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }
        Label {
            text: "Searchable daily summaries, transcripts, and detected events."
            color: "#565f89"; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }

        TextField {
            id: search
            Layout.fillWidth: true
            placeholderText: "Search memory…"
            text: timelineModel.filter
            // Debounce so we don't re-query SQLite on every keystroke.
            onTextChanged: searchTimer.restart()
            Timer {
                id: searchTimer; interval: 250
                onTriggered: timelineModel.setFilter(search.text)
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: 10; color: "#171a21"; border.color: "#24283b"

            // Backed by the C++ TimelineModel (merged events + transcripts +
            // memories). Live detections/utterances are prepended in place.
            ListView {
                id: list
                anchors.fill: parent; anchors.margins: 8
                clip: true; spacing: 4
                model: timelineModel

                delegate: Rectangle {
                    id: erow
                    required property string category
                    required property string kind
                    required property string text
                    required property string timeLabel
                    width: ListView.view ? ListView.view.width : 0
                    height: body.implicitHeight + 14
                    radius: 6; color: "#1f2335"

                    function categoryColor(c) {
                        switch (c) {
                            case "event":      return "#bb9af7"
                            case "transcript": return "#7dcfff"
                            case "memory":     return "#9ece6a"
                            default:           return "#565f89"
                        }
                    }

                    RowLayout {
                        anchors.fill: parent; anchors.margins: 7; spacing: 10
                        ColumnLayout {
                            Layout.alignment: Qt.AlignTop; spacing: 0
                            Label {
                                text: erow.timeLabel
                                color: "#565f89"; font.pixelSize: 11
                            }
                            Label {
                                text: erow.kind
                                color: erow.categoryColor(erow.category); font.pixelSize: 11; font.bold: true
                            }
                        }
                        Label {
                            id: body
                            Layout.fillWidth: true
                            text: erow.text
                            color: "#c0caf5"; wrapMode: Text.WordWrap
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: search.text.length > 0 ? "No matches." : "timeline appears here"
                    color: "#565f89"
                }
            }
        }
    }
}
