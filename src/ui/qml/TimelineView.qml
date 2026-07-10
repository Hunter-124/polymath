import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

Item {
    Component.onCompleted: app.refreshTimeline()

    ColumnLayout {
        anchors.fill: parent; anchors.margins: Style.pad; spacing: Style.gap
        Label {
            text: "Memory & Timeline"; color: Style.text
            font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
        }
        Label {
            text: "Searchable daily summaries, transcripts, and detected events."
            color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsBody
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }

        PmTextField {
            id: search
            Layout.fillWidth: true
            placeholderText: "Search memory…"
            text: timelineModel.filter
            onTextChanged: searchTimer.restart()
            Timer {
                id: searchTimer; interval: 250
                onTriggered: timelineModel.setFilter(search.text)
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: Style.radius; color: Style.surface; border.color: Style.borderSoft

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
                    height: body.implicitHeight + 16
                    radius: Style.radiusXs; color: Style.surface2

                    function categoryColor(c) {
                        switch (c) {
                            case "event":      return Style.magenta
                            case "transcript": return Style.info
                            case "memory":     return Style.good
                            default:           return Style.textFaint
                        }
                    }

                    RowLayout {
                        anchors.fill: parent; anchors.margins: 9; spacing: 12
                        // colour rail by category
                        Rectangle {
                            width: 3; Layout.fillHeight: true; radius: 2
                            color: erow.categoryColor(erow.category)
                        }
                        ColumnLayout {
                            Layout.alignment: Qt.AlignTop; spacing: 1
                            Layout.preferredWidth: 92
                            Label {
                                text: erow.timeLabel; color: Style.textFaint
                                font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                            }
                            Label {
                                text: erow.kind; color: erow.categoryColor(erow.category)
                                font.family: Style.fontFamily; font.pixelSize: Style.fsTiny; font.bold: true
                            }
                        }
                        Label {
                            id: body
                            Layout.fillWidth: true
                            text: erow.text; color: Style.text
                            font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: list.count === 0
                    glyph: search.text.length > 0 ? "?" : "○"
                    title: search.text.length > 0 ? "No matches" : "Your timeline is empty"
                    hint: search.text.length > 0
                        ? "Nothing in memory matches “" + search.text + "”. Try a broader term."
                        : "As Polymath listens and watches, detected events, transcripts and daily summaries collect here — fully searchable."
                }
            }
        }
    }
}
