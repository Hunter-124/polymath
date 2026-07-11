import QtQuick
import QtQuick.Layouts
import Polymath

// PlaceholderSurface — clean titled placeholder (02 §F5). Used by the
// `placeholder` and `monitor` surface types when no richer surface (note/
// image/web/video) applies. E3: retired the raw argsJson dump in favor of a
// calm "waiting" state — argsJson is still accepted for compatibility but
// is no longer rendered verbatim.
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
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            Item { Layout.fillHeight: true }
            Text {
                text: "Waiting for content…"
                color: Style.textFaint
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
        }
    }
}
