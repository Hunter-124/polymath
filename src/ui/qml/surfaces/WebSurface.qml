import QtQuick
import QtQuick.Layouts
import Polymath

// WebSurface — graceful placeholder until QtWebEngine (D5 / 02 §F5).
GlassCard {
    id: root
    property string title: "Web"
    property string url: ""
    property string argsJson: ""
    section: "Chat"
    implicitWidth: 360
    implicitHeight: 220

    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - Style.pad * 2
        spacing: Style.gapSm

        PmIcon {
            Layout.alignment: Qt.AlignHCenter
            name: "search"
            color: Style.sectionColor("Chat")
            width: Style.iconLg
            height: Style.iconLg
        }
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
            text: root.url.length > 0
                  ? root.url
                  : "Install QtWebEngine for live web/YouTube surfaces."
            color: Style.textFaint
            font.family: Style.fontFamily
            font.pixelSize: Style.fsSmall
            wrapMode: Text.WordWrap
        }
    }
}
