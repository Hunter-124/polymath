import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// NoteSurface — research-board text card (E3, type:"note"). Title + a
// scrollable markdown body (args.md), glass card, capped default height so
// it reads clean sitting next to media in a `board` layout (03 owner ask:
// "clean bounding boxes with pictures alongside information").
GlassCard {
    id: root
    property string title: "Note"
    property string argsJson: ""
    // Host (SurfaceHost) sets this directly from the spawn/A3 `md` field;
    // also readable from args.md for callers that only set argsJson.
    property string md: ""
    property string group: ""
    section: "Chat"
    implicitWidth: 320
    implicitHeight: 280

    // Sensible cap so a very long note never dominates a board/tile layout;
    // beyond this the body scrolls (Flickable below) instead of growing.
    // (SurfaceHost always assigns an explicit width/height via its layout
    // cache, so this is a fallback/default only.)
    readonly property real maxHeight: 420

    readonly property var args: {
        if (!argsJson || argsJson.length === 0) return ({})
        try { return JSON.parse(argsJson) } catch (e) { return ({}) }
    }
    readonly property string displayTitle: (args && args.title && String(args.title).length > 0)
                                          ? args.title : root.title
    readonly property string markdown: root.md.length > 0 ? root.md
                                      : ((args && args.md) ? args.md
                                      : ((args && args.text) ? args.text : ""))

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.gapSm
        spacing: Style.gapSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gapSm
            PmIcon {
                name: "sparkle"
                color: Style.sectionColor("Chat")
                Layout.preferredWidth: Style.iconMd
                Layout.preferredHeight: Style.iconMd
            }
            Text {
                text: root.displayTitle
                color: Style.text
                font.family: Style.fontFamily
                font.pixelSize: Style.fsHeading
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Style.glassBorder
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Chat") }

            Text {
                id: body
                width: flick.width
                text: root.markdown.length > 0 ? root.markdown : "*Empty note*"
                textFormat: Text.MarkdownText
                wrapMode: Text.WordWrap
                color: Style.textDim
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
                linkColor: Style.accent
                onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            }
        }
    }
}
