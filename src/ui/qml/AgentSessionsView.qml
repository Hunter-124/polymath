import QtQuick
import QtQuick.Layouts
import Polymath

// AgentSessionsView — placeholder page (full impl: C4).
Item {
    id: root
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap
        Text {
            text: "Agents"
            color: Style.text
            font.family: Style.fontFamily
            font.pixelSize: Style.fsTitle
            font.bold: true
        }
        Text {
            text: "External agent sessions (Claude Code, Codex, …) land in C4."
            color: Style.textDim
            font.family: Style.fontFamily
            font.pixelSize: Style.fsBody
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Item { Layout.fillHeight: true }
    }
}
