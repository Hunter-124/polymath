import QtQuick
import QtQuick.Layouts
import Polymath

// EmptyState — centred placeholder. Keeps all public props from §0.
Item {
    id: root
    property string glyph: "●"
    property string title: ""
    property string hint: ""
    property color  glyphColor: Style.textFaint
    property string iconName: ""
    property alias  actionText: action.text
    property bool   actionVisible: false
    signal actionTriggered()

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 440)
        spacing: 12

        // Glass disc badge + sectionGlow ring
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: 64; height: 64; radius: 32
            color: Qt.rgba(root.glyphColor.r, root.glyphColor.g, root.glyphColor.b, 0.10)
            border.width: 1
            border.color: Qt.rgba(root.glyphColor.r, root.glyphColor.g, root.glyphColor.b, 0.45)

            PmIcon {
                anchors.centerIn: parent
                width: 28; height: 28
                visible: root.iconName.length > 0
                name: root.iconName
                color: root.glyphColor
            }
            Text {
                anchors.centerIn: parent
                visible: root.iconName.length === 0
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
