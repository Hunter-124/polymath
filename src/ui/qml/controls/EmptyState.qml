import QtQuick
import QtQuick.Layouts
import Polymath

// EmptyState — a centred placeholder for empty / loading / error states.
// Shows a drawn circular badge (font-independent so it never renders as tofu),
// a title, a hint line, and an optional call-to-action.  Views drop one into a
// list/grid and bind `visible` to count===0.
Item {
    id: root
    // A single char drawn inside the badge.  Keep to widely-available glyphs
    // (●, ○, +, ?, !) — the offscreen software renderer has no emoji/symbol
    // fallback, so exotic code points render as tofu.
    property string glyph: "●"
    property string title: ""
    property string hint: ""
    property color  glyphColor: Style.textFaint
    property alias  actionText: action.text
    property bool   actionVisible: false
    signal actionTriggered()

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 440)
        spacing: 12

        // Drawn badge — a soft ring with the glyph centred. Uses the default UI
        // font (not forced to Inter) so the glyph always has a fallback.
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: 64; height: 64; radius: 32
            color: Qt.rgba(root.glyphColor.r, root.glyphColor.g, root.glyphColor.b, 0.12)
            border.width: 1
            border.color: Qt.rgba(root.glyphColor.r, root.glyphColor.g, root.glyphColor.b, 0.5)
            Text {
                anchors.centerIn: parent
                text: root.glyph
                color: root.glyphColor
                font.pixelSize: 28
            }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: root.title.length > 0
            text: root.title
            color: Style.text
            font.family: Style.fontFamily
            font.pixelSize: Style.fsHeading
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            Layout.fillWidth: true
            visible: root.hint.length > 0
            text: root.hint
            color: Style.textFaint
            font.family: Style.fontFamily
            font.pixelSize: Style.fsBody
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
        PmButton {
            id: action
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            visible: root.actionVisible && text.length > 0
            accent: true
            onClicked: root.actionTriggered()
        }
    }
}
