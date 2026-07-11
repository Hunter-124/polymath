import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Cameras — teal glass tiles (01 §5.3).
Item {
    id: root
    Component.onCompleted: {
        app.refreshCameras()
        reloadUsers()
    }

    property var userRows: []
    property var activeUid: -1
    function reloadUsers() {
        if (typeof app !== "undefined" && app.listUsers) {
            userRows = app.listUsers()
            activeUid = app.activeUserId ? app.activeUserId() : -1
        }
    }
    function createAndEnroll() {
        if (!newUserName.text.trim()) return
        if (typeof app === "undefined" || !app.createUser) return
        var id = app.createUser(newUserName.text.trim())
        if (id > 0 && app.enrollUserFace) app.enrollUserFace(id)
        newUserName.text = ""
        reloadUsers()
    }

    // Periodic nudge so MJPEG-style sources repaint even without a model tick.
    property int refreshTick: 0
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: root.refreshTick++
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Cameras"
            section: "Cameras"
            subtitle: "Live tiles and object find"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gap
            PmTextField {
                id: q
                Layout.fillWidth: true
                tone: Style.sectionColor("Cameras")
                placeholderText: "Find an object…  (e.g. my keys, the cat, a red mug)"
                onAccepted: app.findObject(q.text)
            }
            PmButton {
                text: "Find"
                accent: true
                tone: Style.sectionColor("Cameras")
                onClicked: app.findObject(q.text)
            }
        }

        // Wave Z: household users + face enroll
        GlassCard {
            Layout.fillWidth: true
            Layout.preferredHeight: usersCol.implicitHeight + 24
            section: "Cameras"
            ColumnLayout {
                id: usersCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Style.gapSm
                spacing: Style.gapSm
                Text {
                    text: "Household users (face → memory namespace)"
                    color: Style.textDim
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsSmall
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Style.gap
                    PmTextField {
                        id: newUserName
                        Layout.fillWidth: true
                        tone: Style.sectionColor("Cameras")
                        placeholderText: "Name to enroll…"
                        onAccepted: root.createAndEnroll()
                    }
                    PmButton {
                        text: "Create + enroll"
                        onClicked: root.createAndEnroll()
                    }
                    PmButton {
                        text: "Refresh users"
                        onClicked: root.reloadUsers()
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: root.userRows
                        delegate: RowLayout {
                            required property var modelData
                            spacing: 4
                            PmButton {
                                text: (modelData.name || "?") +
                                      (Number(modelData.id) === Number(root.activeUid) ? " ●" : "")
                                onClicked: {
                                    if (typeof app !== "undefined") {
                                        app.setActiveUserId(modelData.id)
                                        root.activeUid = modelData.id
                                    }
                                }
                            }
                            PmButton {
                                text: "Enroll"
                                onClicked: {
                                    if (typeof app !== "undefined" && app.enrollUserFace)
                                        app.enrollUserFace(modelData.id)
                                }
                            }
                        }
                    }
                }
                Text {
                    text: "Click a user to set active memory namespace. Long-press to re-enroll face."
                    color: Style.textFaint
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsTiny
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        // Live ESP32-CAM tiles via CameraImageProvider ("image://cameras/<id>").
        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Cameras"

            GridView {
                id: grid
                anchors.fill: parent
                anchors.margins: Style.gapSm
                clip: true
                cellWidth: Math.max(260, width / Math.max(1, Math.floor(width / 360)))
                cellHeight: cellWidth * 0.62 + 30
                model: cameraModel
                ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Cameras") }

                delegate: Item {
                    id: tile
                    required property int cameraId
                    required property string name
                    required property string location
                    required property bool enabled
                    required property bool live
                    required property int frameTick
                    width: grid.cellWidth
                    height: grid.cellHeight

                    GlassCard {
                        anchors.fill: parent
                        anchors.margins: 6
                        section: "Cameras"

                        // Overlay border for live state
                        Rectangle {
                            anchors.fill: parent
                            radius: Style.radius
                            color: "transparent"
                            border.width: tile.live ? 2 : 1
                            border.color: tile.live
                                          ? Style.sectionGlow("Cameras", 0.75)
                                          : Style.glassBorder
                            z: 3
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Style.gapSm
                            spacing: 6
                            z: 2

                            // Image well — keep near-black for feed contrast (spec 01 §5.3)
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: Style.radiusXs
                                color: "#0b0d12"
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: false
                                    source: tile.enabled
                                        ? "image://cameras/" + tile.cameraId
                                          + "?t=" + tile.frameTick + "_" + root.refreshTick
                                        : ""
                                }
                                Label {
                                    anchors.centerIn: parent
                                    visible: !tile.enabled
                                    text: "disabled"
                                    color: Style.textFaint
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsSmall
                                }
                                Label {
                                    anchors.centerIn: parent
                                    visible: tile.enabled && !tile.live
                                    text: "connecting…"
                                    color: Style.textFaint
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsSmall
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Label {
                                    text: tile.name + (tile.location.length ? ("  ·  " + tile.location) : "")
                                    color: Style.text
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsSmall
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                PmStatusDot {
                                    tone: tile.live ? Style.sectionColor("Cameras") : Style.textFaint
                                    pulsing: tile.live
                                    size: 8
                                    Layout.alignment: Qt.AlignVCenter
                                }
                                PmBadge {
                                    text: tile.live ? "live" : "offline"
                                    tone: tile.live ? Style.good : Style.textFaint
                                }
                            }
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: grid.count === 0
                    glyph: "o"
                    iconName: "camera"
                    glyphColor: Style.sectionColor("Cameras")
                    title: "No cameras configured"
                    hint: "Flash the ESP32-CAM firmware and add a camera so its live tile appears here. Enable Cameras in Privacy first if it is switched off."
                }
            }
        }

        Connections {
            target: app
            function onFindObjectAnswered(query, answer) {
                ans.text = "\"" + query + "\"  →  " + answer
                ans.visible = true
            }
        }
        GlassCard {
            visible: ans.visible
            Layout.fillWidth: true
            Layout.preferredHeight: ans.implicitHeight + 18
            section: "Cameras"
            Label {
                id: ans
                anchors.fill: parent
                anchors.margins: 9
                visible: false
                color: Style.sectionColor("Cameras")
                wrapMode: Text.WordWrap
                font.family: Style.fontFamily
                font.pixelSize: Style.fsBody
            }
        }
    }
}
