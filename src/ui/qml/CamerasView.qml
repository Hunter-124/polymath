import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

Item {
    id: root
    Component.onCompleted: app.refreshCameras()

    // Periodic nudge so MJPEG-style sources repaint even without a model tick.
    property int refreshTick: 0
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: root.refreshTick++
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: Style.pad; spacing: Style.gap
        Label {
            text: "Cameras"; color: Style.text
            font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true; spacing: Style.gap
            PmTextField {
                id: q; Layout.fillWidth: true
                placeholderText: "Find an object…  (e.g. my keys, the cat, a red mug)"
                onAccepted: app.findObject(q.text)
            }
            PmButton { text: "Find"; accent: true; onClicked: app.findObject(q.text) }
        }

        // Live ESP32-CAM tiles via CameraImageProvider ("image://cameras/<id>").
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: Style.radius; color: Style.surface; border.color: Style.borderSoft

            GridView {
                id: grid
                anchors.fill: parent; anchors.margins: 8
                clip: true
                cellWidth: Math.max(260, width / Math.max(1, Math.floor(width / 360)))
                cellHeight: cellWidth * 0.62 + 30
                model: cameraModel

                delegate: Item {
                    id: tile
                    required property int cameraId
                    required property string name
                    required property string location
                    required property bool enabled
                    required property bool live
                    required property int frameTick
                    width: grid.cellWidth; height: grid.cellHeight

                    Rectangle {
                        anchors.fill: parent; anchors.margins: 6
                        radius: Style.radius; color: Style.surface2
                        border.color: tile.live ? Style.good : Style.border
                        border.width: tile.live ? 2 : 1

                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 8; spacing: 6

                            Rectangle {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                radius: Style.radiusXs; color: "#0b0d12"; clip: true
                                Image {
                                    anchors.fill: parent
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: false
                                    source: tile.enabled
                                        ? "image://cameras/" + tile.cameraId + "?t=" + tile.frameTick + "_" + root.refreshTick
                                        : ""
                                }
                                Label {
                                    anchors.centerIn: parent
                                    visible: !tile.enabled
                                    text: "○  disabled"; color: Style.textFaint
                                    font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                }
                                // "waiting for frames" overlay while enabled but not yet live.
                                Label {
                                    anchors.centerIn: parent
                                    visible: tile.enabled && !tile.live
                                    text: "○  connecting…"; color: Style.textFaint
                                    font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                Label {
                                    text: tile.name + (tile.location.length ? ("  ·  " + tile.location) : "")
                                    color: Style.text; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                    elide: Text.ElideRight; Layout.fillWidth: true
                                }
                                Label {
                                    text: tile.live ? "● live" : "○ offline"
                                    color: tile.live ? Style.good : Style.textFaint
                                    font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                                }
                            }
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: grid.count === 0
                    glyph: "○"
                    title: "No cameras configured"
                    hint: "Flash the ESP32-CAM firmware and add a camera so its live tile appears here. Enable Cameras in Privacy first if it is switched off."
                }
            }
        }

        Connections {
            target: app
            function onFindObjectAnswered(query, answer) {
                ans.text = "“" + query + "”  →  " + answer
                ans.visible = true
            }
        }
        Rectangle {
            visible: ans.visible
            Layout.fillWidth: true
            radius: Style.radiusSm; color: Style.surface2; border.color: Style.good
            implicitHeight: ans.implicitHeight + 18
            Label {
                id: ans
                anchors.fill: parent; anchors.margins: 9
                visible: false
                color: Style.good; wrapMode: Text.WordWrap
                font.family: Style.fontFamily; font.pixelSize: Style.fsBody
            }
        }
    }
}
