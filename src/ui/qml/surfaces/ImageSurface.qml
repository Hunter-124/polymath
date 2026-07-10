import QtQuick
import QtQuick.Layouts
import Polymath

// ImageSurface — image from args url/path (02 §F5).
GlassCard {
    id: root
    property string title: "Image"
    property string source: ""
    property string argsJson: ""
    section: "Cameras"
    implicitWidth: 320
    implicitHeight: 200

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.gapSm
        spacing: 4

        Text {
            text: root.title
            color: Style.text
            font.family: Style.fontFamily
            font.pixelSize: Style.fsSmall
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Style.radiusXs
            color: "#0b0d12"
            clip: true

            Image {
                anchors.fill: parent
                source: root.source
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }

            Text {
                anchors.centerIn: parent
                visible: root.source.length === 0
                text: "No image source"
                color: Style.textFaint
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
            }
        }
    }
}
