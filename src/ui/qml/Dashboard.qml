import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

Item {
    id: root
    // The real AppController has no hasModels yet; treat "no model loaded" as the
    // cold-start signal so the first-run banner still works there.
    readonly property bool hasModels:
        (app.hasModels !== undefined) ? app.hasModels
        : app.modelStatus !== "no model loaded"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gapLg

        Label {
            text: "Dashboard"; color: Style.text
            font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
        }

        // Cold-start banner: no Fast model -> guide to fetch-models.
        Rectangle {
            visible: !root.hasModels
            Layout.fillWidth: true
            radius: Style.radiusSm; color: Style.surface2
            border.width: 1; border.color: Style.warn
            implicitHeight: coldCol.implicitHeight + 24
            RowLayout {
                id: coldRow
                anchors.fill: parent; anchors.margins: 12; spacing: 12
                Rectangle {
                    width: 26; height: 26; radius: 13
                    color: Qt.rgba(Style.warn.r, Style.warn.g, Style.warn.b, 0.18)
                    border.width: 1; border.color: Style.warn
                    Layout.alignment: Qt.AlignVCenter
                    Text { anchors.centerIn: parent; text: "!"; color: Style.warn; font.bold: true; font.pixelSize: 16 }
                }
                ColumnLayout {
                    id: coldCol
                    Layout.fillWidth: true; spacing: 2
                    Label {
                        text: "No model loaded — finish setup"
                        color: Style.warn; font.family: Style.fontFamily
                        font.pixelSize: Style.fsBody; font.bold: true
                    }
                    Label {
                        text: "Run  scripts/fetch-models.ps1  to download the default local models, then open Models to confirm roles."
                        color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
            }
        }

        GridLayout {
            columns: 3; columnSpacing: Style.gapLg; rowSpacing: Style.gapLg
            Layout.fillWidth: true

            component Card: Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 124
                radius: Style.radius; color: Style.surface; border.color: Style.borderSoft
            }

            Card {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 4
                    Label { text: "Assistant"; color: Style.accent; font.family: Style.fontFamily; font.bold: true }
                    RowLayout {
                        spacing: 8
                        Rectangle {
                            width: 10; height: 10; radius: 5
                            color: app.listening ? Style.good : Style.textFaint
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Label {
                            text: app.listening ? "Listening" : "Idle"; color: Style.text
                            font.family: Style.fontFamily; font.pixelSize: 22
                        }
                    }
                    Label { text: app.activePersonality; color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall }
                }
            }
            Card {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 4
                    Label { text: "Model"; color: Style.accent; font.family: Style.fontFamily; font.bold: true }
                    Label {
                        text: app.modelStatus; color: root.hasModels ? Style.text : Style.warn
                        font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
            }
            Card {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 4
                    Label { text: "Today"; color: Style.accent; font.family: Style.fontFamily; font.bold: true }
                    Label {
                        text: "Reminders & suggestions appear here"; color: Style.textFaint
                        font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: Style.radius; color: Style.surface; border.color: Style.borderSoft
            EmptyState {
                anchors.fill: parent
                glyph: "+"
                glyphColor: Style.accent
                title: "Ask Polymath anything"
                hint: "Open Chat or hold Push-to-talk. Try \"where did I leave my keys?\", \"add milk to the shopping list\", or \"summarise today\"."
            }
        }
    }
}
