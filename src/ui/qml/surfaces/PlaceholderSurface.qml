import QtQuick
import QtQuick.Layouts
import Polymath

// PlaceholderSurface — titled glass panel echoing args (02 §F5).
GlassCard {
    id: root
    property string title: "Surface"
    property string argsJson: ""
    section: "Agents"
    // Host sizes us; keep sensible minimums
    implicitWidth: 280
    implicitHeight: 160

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.padSm
        spacing: Style.gapSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gapSm
            PmIcon {
                name: "sparkle"
                color: Style.sectionColor("Agents")
                Layout.preferredWidth: Style.iconMd
                Layout.preferredHeight: Style.iconMd
            }
            Text {
                text: root.title
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
        Text {
            text: root.argsJson.length > 0 ? root.argsJson : "placeholder surface"
            color: Style.textDim
            font.family: Style.fontFamily
            font.pixelSize: Style.fsSmall
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
