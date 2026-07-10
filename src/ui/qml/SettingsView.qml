import QtQuick
import QtQuick.Layouts
import Polymath

// SettingsView — placeholder (full impl: wave B6). Accepts focusSection deep-link.
Item {
    id: root
    property string focusSection: ""
    anchors.fill: parent

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap
        Text {
            text: "Settings"
            color: Style.text
            font.family: Style.fontFamily
            font.pixelSize: Style.fsTitle
            font.bold: true
        }
        Text {
            text: root.focusSection.length > 0
                  ? ("Section: " + root.focusSection + " (stub)")
                  : "Appearance · Audio · Search · Behavior · Agents — full UI in B6"
            color: Style.textDim
            font.family: Style.fontFamily
            font.pixelSize: Style.fsBody
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Item { Layout.fillHeight: true }
    }
}
