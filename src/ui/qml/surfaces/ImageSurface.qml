import QtQuick
import QtQuick.Layouts
import Polymath

// ImageSurface — image from args url/path (02 §F5). E3 adds: optional
// caption bar (args.caption), a fit/fill toggle, and click-to-focus (asks
// SurfaceHost to arrange this surface full-screen, toggling back on a
// second click) — the research-board "picture with info alongside it" card.
GlassCard {
    id: root
    property string title: "Image"
    property string source: ""
    property string argsJson: ""
    property string caption: ""
    property string surfaceId: ""
    property string group: ""
    section: "Cameras"
    implicitWidth: 320
    implicitHeight: 200

    // SurfaceHost connects this dynamically (Loader.onLoaded), same
    // contract as VideoPickerSurface.requestSpawn / WebSurface.requestClose.
    signal requestFocus(string id)

    // fit (aspect-preserving letterbox) vs fill (aspect-preserving crop)
    property bool fillMode: false

    readonly property var args: {
        if (!argsJson || argsJson.length === 0) return ({})
        try { return JSON.parse(argsJson) } catch (e) { return ({}) }
    }
    readonly property string displayCaption: root.caption.length > 0 ? root.caption
                                            : ((args && args.caption) ? args.caption : "")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.gapSm
        spacing: 4

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
            PmButton {
                text: root.fillMode ? "Fill" : "Fit"
                flat: true
                implicitHeight: 24
                onClicked: root.fillMode = !root.fillMode
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Style.radiusXs
            color: "#0b0d12"
            clip: true

            Image {
                anchors.fill: parent
                source: root.source
                fillMode: root.fillMode ? Image.PreserveAspectCrop : Image.PreserveAspectFit
                asynchronous: true
            }

            Text {
                anchors.centerIn: parent
                visible: root.source.length === 0
                text: "No image source"
                color: Style.textFaint
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.requestFocus(root.surfaceId)
            }
        }

        Text {
            visible: root.displayCaption.length > 0
            text: root.displayCaption
            color: Style.textDim
            font.family: Style.fontFamily
            font.pixelSize: Style.fsTiny
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
