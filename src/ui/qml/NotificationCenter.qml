import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// NotificationCenter — glass popover over notifications model (02 §F3).
Item {
    id: root
    property bool open: false
    // Optional: parent can bind a navigate(category, id) handler
    signal navigateRequest(string category, string id)

    width: 340
    height: open ? 400 : 0
    visible: open
    clip: true
    z: 40

    onOpenChanged: {
        if (open && typeof notifications !== "undefined" && notifications)
            notifications.markAllRead()
    }

    function severityTone(s) {
        if (s === "error") return Style.bad
        if (s === "warn")  return Style.warn
        if (s === "good")  return Style.good
        return Style.info
    }

    Behavior on height {
        NumberAnimation {
            duration: Style.reduceMotion ? Style.durFast : Style.durBase
            easing.type: Easing.OutCubic
        }
    }

    GlassPanel {
        anchors.fill: parent
        radius: Style.radius
        elevation: 3
        tintColor: Style.accent
        tintAlpha: 0.06

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Style.padSm
            spacing: Style.gapSm

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "Notifications"
                    color: Style.text
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsHeading
                    font.bold: true
                    Layout.fillWidth: true
                }
                PmButton {
                    text: "Clear all"
                    flat: true
                    onClicked: {
                        if (typeof notifications !== "undefined" && notifications)
                            notifications.clearAll()
                    }
                }
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: (typeof notifications !== "undefined") ? notifications : null
                ScrollBar.vertical: PmScrollBar { }

                delegate: Rectangle {
                    id: nrow
                    // Role "id" is reserved in QML — pull via model.*
                    required property var model
                    required property string severity
                    required property string source
                    required property string title
                    required property string body
                    required property string timeLabel
                    required property bool read
                    required property string category
                    readonly property string notifId: String(model.id || "")

                    width: ListView.view ? ListView.view.width : 0
                    height: col.implicitHeight + Style.gap
                    radius: Style.radiusSm
                    color: rowHover.hovered ? Qt.rgba(1, 1, 1, 0.05)
                         : (!nrow.read ? Style.tint(Style.accent, 0.08) : "transparent")
                    border.width: !nrow.read ? 1 : 0
                    border.color: Style.tint(Style.accent, 0.25)
                    HoverHandler { id: rowHover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Style.gapSm
                        spacing: Style.gapSm

                        PmStatusDot {
                            tone: root.severityTone(nrow.severity)
                            size: 8
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 4
                        }
                        ColumnLayout {
                            id: col
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: nrow.source
                                    color: Style.textFaint
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsTiny
                                    font.bold: true
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: nrow.timeLabel
                                    color: Style.textFaint
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsTiny
                                }
                            }
                            Text {
                                text: nrow.title
                                color: Style.text
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsBody
                                font.bold: !nrow.read
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                visible: nrow.body.length > 0
                                text: nrow.body
                                color: Style.textDim
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsSmall
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (typeof notifications !== "undefined" && notifications)
                                notifications.markRead(nrow.notifId)
                            if (nrow.category === "task" || nrow.category === "reminder")
                                root.navigateRequest(nrow.category, nrow.notifId)
                        }
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: list.count === 0
                    glyph: "o"
                    iconName: "bell"
                    glyphColor: Style.textFaint
                    title: "All clear"
                    hint: "Notices, tasks, and reminders will land here."
                }
            }
        }
    }
}
