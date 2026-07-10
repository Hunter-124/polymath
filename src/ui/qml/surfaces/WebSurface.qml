import QtQuick
import QtQuick.Layouts
import Polymath

// WebSurface — graceful placeholder until QtWebEngine (D5).
GlassCard {
    id: root
    property string title: "Web"
    property string url: ""
    section: "Chat"
    width: 360
    height: 220

    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - Style.pad * 2
        spacing: Style.gapSm
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.title
            color: Style.text
            font.family: Style.fontFamily
            font.pixelSize: Style.fsHeading
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: "WebEngine not installed"
            color: Style.warn
            font.family: Style.fontFamily
            font.pixelSize: Style.fsBody
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.url.length > 0 ? root.url : "Install QtWebEngine for live web/YouTube surfaces."
            color: Style.textFaint
            font.family: Style.fontFamily
            font.pixelSize: Style.fsSmall
            wrapMode: Text.WordWrap
        }
    }
}
