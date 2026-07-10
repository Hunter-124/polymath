import QtQuick
import QtQuick.Layouts
import Polymath

// PlaceholderSurface — titled glass panel echoing args (full polish: B9).
GlassCard {
    id: root
    property string title: "Surface"
    property string argsJson: ""
    section: "Agents"
    width: 280
    height: 160

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.padSm
        spacing: Style.gapSm
        Text {
            text: root.title
            color: Style.text
            font.family: Style.fontFamily
            font.pixelSize: Style.fsHeading
            font.bold: true
        }
        Text {
            text: root.argsJson.length > 0 ? root.argsJson : "placeholder"
            color: Style.textDim
            font.family: Style.fontFamily
            font.pixelSize: Style.fsSmall
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
