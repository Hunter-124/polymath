import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// VideoPickerSurface — "model searched, user selects" (B2). Grid of thumbnail
// cards built from args.results (the youtube_search tool's compact JSON
// shape: {videoId, title, channel, durationSec, views, publishedText,
// thumbnailUrl, watchUrl}). Clicking a card asks the host to replace THIS
// surface (same id/slot) with a playing "video" surface.
GlassCard {
    id: root
    property string title: "Search results"
    property string argsJson: ""
    property string surfaceId: ""
    section: "Chat"
    implicitWidth: 560
    implicitHeight: 380

    // SurfaceHost connects these dynamically (Loader.onLoaded) — same
    // mechanism WebSurface uses for requestClose. Surfaces can't reach
    // AppController/EventBus directly, so a plain QML signal is the contract.
    signal requestSpawn(string id, string type, string title, string argsJson)
    signal requestClose()

    readonly property var args: {
        if (!argsJson || argsJson.length === 0) return ({})
        try { return JSON.parse(argsJson) } catch (e) { return ({}) }
    }

    // Defensive: args.results (or args itself, if the caller passed the array
    // straight through) may arrive as a JSON string or an already-parsed array.
    readonly property var results: {
        var r = (args && args.results !== undefined) ? args.results : args
        if (typeof r === "string") {
            try { r = JSON.parse(r) } catch (e) { r = [] }
        }
        return Array.isArray(r) ? r : []
    }

    function formatDuration(sec) {
        if (sec === undefined || sec === null || sec === "") return ""
        var s = Math.max(0, Math.round(Number(sec) || 0))
        var m = Math.floor(s / 60)
        var rem = s % 60
        return m + ":" + (rem < 10 ? "0" + rem : rem)
    }

    function pick(item) {
        var vArgs = {
            videoId: item.videoId || "",
            title: item.title || "",
            url: item.watchUrl || ""
        }
        root.requestSpawn(root.surfaceId, "video", item.title || "Video", JSON.stringify(vArgs))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.gapSm
        spacing: Style.gapSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gapSm
            Text {
                text: root.title
                color: Style.text
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            PmPill { text: root.results.length + " results" }
            PmToolButton {
                iconName: "x"
                onClicked: root.requestClose()
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.max(180, width / Math.max(1, Math.floor(width / 200)))
            cellHeight: 186
            model: root.results
            ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Chat") }

            delegate: Item {
                id: cardWrap
                required property var modelData
                width: grid.cellWidth - Style.gapSm
                height: grid.cellHeight - Style.gapSm

                GlassCard {
                    id: card
                    anchors.fill: parent
                    section: "Chat"
                    radius: Style.radiusSm
                    hovered: cardMouse.containsMouse

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Style.gapSm
                        spacing: 4

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 96
                            radius: Style.radiusXs
                            color: "#0b0d12"
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: cardWrap.modelData.thumbnailUrl || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: !cardWrap.modelData.thumbnailUrl
                                text: "No thumbnail"
                                color: Style.textFaint
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny
                            }

                            Rectangle {
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 4
                                visible: root.formatDuration(cardWrap.modelData.durationSec).length > 0
                                radius: Style.radiusXs
                                color: Qt.rgba(0, 0, 0, 0.72)
                                implicitWidth: durLabel.implicitWidth + 8
                                implicitHeight: durLabel.implicitHeight + 4

                                Text {
                                    id: durLabel
                                    anchors.centerIn: parent
                                    text: root.formatDuration(cardWrap.modelData.durationSec)
                                    color: Style.text
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsTiny
                                    font.bold: true
                                }
                            }
                        }

                        Text {
                            text: cardWrap.modelData.title || ""
                            color: Style.text
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                            font.bold: true
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Text {
                            text: [cardWrap.modelData.channel, cardWrap.modelData.views,
                                   cardWrap.modelData.publishedText]
                                    .filter(function(s) { return s !== undefined && s !== null && String(s).length > 0 })
                                    .join(" · ")
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsTiny
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    MouseArea {
                        id: cardMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.pick(cardWrap.modelData)
                    }
                }
            }
        }

        Text {
            visible: root.results.length === 0
            text: "No results"
            color: Style.textFaint
            font.family: Style.fontFamily
            font.pixelSize: Style.fsSmall
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
