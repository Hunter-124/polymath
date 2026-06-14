import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

Item {
    id: root
    // app.hasModels is a live property on AppController (true once a usable model
    // is registered on disk); the cold-start banner shows while it is false.
    readonly property bool hasModels: app.hasModels

    Component.onCompleted: app.refreshAll()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gapLg

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Dashboard"; color: Style.text
                font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
            }
            Item { Layout.fillWidth: true }
            Label {
                text: Qt.formatDate(new Date(), "dddd, MMMM d")
                color: Style.textFaint
                font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
            }
        }

        // Cold-start banner: no Fast model -> guide to Settings ▸ Models.
        Rectangle {
            visible: !root.hasModels
            Layout.fillWidth: true
            radius: Style.radiusSm; color: Style.surface2
            border.width: 1; border.color: Style.warn
            implicitHeight: coldCol.implicitHeight + 24
            RowLayout {
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
                        text: "Open Settings › Models to download Hearth's local models — then it can listen, chat and see. Everything stays on this machine."
                        color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
                PmButton {
                    text: "Open Models"
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: Nav.goSettings("Models")
                }
            }
        }

        // Quick-ask — type here, land in Chat with the reply streaming.
        RowLayout {
            Layout.fillWidth: true; spacing: Style.gap
            PmTextField {
                id: quickAsk
                Layout.fillWidth: true
                placeholderText: "Ask Hearth anything…"
                onAccepted: root.ask()
            }
            PmButton {
                accent: true
                enabled: quickAsk.text.trim().length > 0
                onClicked: root.ask()
                contentItem: RowLayout {
                    spacing: 7
                    Item { Layout.fillWidth: true }
                    PmIcon { width: 15; height: 15; name: "send"; color: Style.accentText }
                    Text {
                        text: "Ask"; color: Style.accentText
                        font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                        verticalAlignment: Text.AlignVCenter
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        GridLayout {
            columns: 3; columnSpacing: Style.gapLg; rowSpacing: Style.gapLg
            Layout.fillWidth: true

            component Card: Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 116
                radius: Style.radius; color: Style.surface; border.color: Style.borderSoft
            }

            Card {
                RowLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 12
                    PersonalityAvatar {
                        Layout.preferredWidth: 48; Layout.preferredHeight: 48
                        Layout.alignment: Qt.AlignVCenter
                        displayName: app.activePersona.name || app.activePersonality
                        avatarStyle: app.activePersona.style || "orb"
                        accent: (app.activePersona.accent && app.activePersona.accent.length)
                                ? app.activePersona.accent : Style.accent
                        idleSource: app.activePersona.idle || ""
                        talkingSource: app.activePersona.talking || ""
                        speaking: app.speaking
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: "Assistant"; color: Style.accent; font.family: Style.fontFamily; font.bold: true; font.pixelSize: Style.fsSmall }
                        Label {
                            text: app.speaking ? "Speaking" : app.listening ? "Listening" : "Idle"
                            color: app.speaking ? Style.accent : Style.text
                            font.family: Style.fontFamily; font.pixelSize: 20
                        }
                        Label {
                            text: app.activePersona.name || app.activePersonality
                            color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                            elide: Text.ElideRight; Layout.fillWidth: true
                        }
                    }
                }
            }
            Card {
                AbstractButton {
                    anchors.fill: parent
                    onClicked: Nav.goSettings("Models")
                    background: null
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 16; spacing: 4
                        Label { text: "Model"; color: Style.accent; font.family: Style.fontFamily; font.bold: true; font.pixelSize: Style.fsSmall }
                        Label {
                            text: app.modelStatus; color: root.hasModels ? Style.text : Style.warn
                            font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                            wrapMode: Text.WordWrap; Layout.fillWidth: true
                            maximumLineCount: 3; elide: Text.ElideRight
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
            Card {
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 6
                    Label { text: "Today"; color: Style.accent; font.family: Style.fontFamily; font.bold: true; font.pixelSize: Style.fsSmall }
                    RowLayout {
                        spacing: 14
                        component Stat: AbstractButton {
                            id: stat
                            property string value: ""
                            property string caption: ""
                            property string page: ""
                            implicitWidth: statCol.implicitWidth
                            implicitHeight: statCol.implicitHeight
                            onClicked: if (page.length) Nav.navigate(page)
                            background: null
                            ColumnLayout {
                                id: statCol
                                spacing: 0
                                Label {
                                    text: stat.value; color: Style.text
                                    font.family: Style.fontFamily; font.pixelSize: 21
                                }
                                Label {
                                    text: stat.caption; color: Style.textFaint
                                    font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                                }
                            }
                        }
                        Stat {
                            value: taskModel.runningCount + taskModel.queuedCount
                            caption: "active tasks"; page: "Tasks"
                        }
                        Stat {
                            value: shoppingModel.remainingCount
                            caption: "to buy"; page: "Shopping"
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        // Recent activity — the head of the timeline feed.
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: Style.radius; color: Style.surface; border.color: Style.borderSoft

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "Recent activity"; color: Style.text
                        font.family: Style.fontFamily; font.pixelSize: Style.fsBody; font.bold: true
                        Layout.leftMargin: 4
                    }
                    Item { Layout.fillWidth: true }
                    PmButton {
                        flat: true; text: "Open Timeline"
                        onClicked: Nav.navigate("Timeline")
                    }
                }

                ListView {
                    id: feed
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: 3
                    model: timelineModel

                    delegate: Rectangle {
                        id: frow
                        required property string category
                        required property string kind
                        required property string text
                        required property string timeLabel
                        width: ListView.view ? ListView.view.width : 0
                        height: 30
                        radius: Style.radiusXs
                        color: frowHover.hovered ? Style.surface2 : "transparent"
                        HoverHandler { id: frowHover }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6; anchors.rightMargin: 8
                            spacing: 10
                            Rectangle {
                                width: 3; height: 14; radius: 2
                                color: frow.category === "event" ? Style.magenta
                                     : frow.category === "transcript" ? Style.info
                                     : frow.category === "memory" ? Style.good : Style.textFaint
                            }
                            Label {
                                text: frow.timeLabel.length > 5
                                      ? frow.timeLabel.slice(-5) : frow.timeLabel
                                color: Style.textFaint
                                font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                                Layout.preferredWidth: 36
                            }
                            Label {
                                text: frow.text; color: Style.textDim
                                font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                elide: Text.ElideRight; Layout.fillWidth: true
                            }
                        }
                    }

                    EmptyState {
                        anchors.fill: parent
                        visible: feed.count === 0
                        glyph: "+"
                        glyphColor: Style.accent
                        title: "Ask Hearth anything"
                        hint: "Use the box above, open Chat, or hold Push-to-talk. As Hearth listens and watches, activity collects here."
                    }
                }
            }
        }
    }

    function ask() {
        const t = quickAsk.text.trim()
        if (t.length === 0) return
        app.sendChat(t)
        quickAsk.text = ""
        Nav.navigate("Chat")
    }
}
