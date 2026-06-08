import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    Component.onCompleted: app.refreshCameras()

    // Periodic nudge: bump a counter the tiles fold into their image source so
    // they reload even if a camera streams without changing the model tick
    // (e.g. an MJPEG source the provider refreshes out of band).
    property int refreshTick: 0
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: root.refreshTick++
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 12
        Label { text: "Cameras"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }

        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: q; Layout.fillWidth: true
                placeholderText: "Find an object… (e.g. my keys)"
                onAccepted: app.findObject(q.text)
            }
            Button { text: "Find"; onClicked: app.findObject(q.text) }
        }

        // Live ESP32-CAM tiles: each renders the latest frame served by the
        // CameraImageProvider ("image://cameras/<id>"). The frameTick role and
        // the periodic refreshTick are folded into the URL to defeat the image
        // cache so new frames actually repaint.
        GridView {
            id: grid
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            cellWidth: Math.max(260, width / Math.max(1, Math.floor(width / 360)))
            cellHeight: cellWidth * 0.62 + 28
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
                    radius: 10; color: "#171a21"; border.color: tile.live ? "#9ece6a" : "#24283b"

                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 6

                        Image {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                            source: tile.enabled
                                ? "image://cameras/" + tile.cameraId + "?t=" + tile.frameTick + "_" + root.refreshTick
                                : ""
                            Label {
                                anchors.centerIn: parent
                                visible: !tile.enabled
                                text: "disabled"; color: "#565f89"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: tile.name + (tile.location.length ? ("  ·  " + tile.location) : "")
                                color: "#c0caf5"; elide: Text.ElideRight; Layout.fillWidth: true
                            }
                            Label {
                                text: tile.live ? "● live" : "○"
                                color: tile.live ? "#9ece6a" : "#565f89"; font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: "No cameras configured."
                color: "#565f89"
            }
        }

        Connections {
            target: app
            function onFindObjectAnswered(query, answer) {
                ans.text = query + " → " + answer
            }
        }
        Label { id: ans; color: "#9ece6a"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    }
}
