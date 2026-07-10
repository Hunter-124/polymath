import QtQuick
import Polymath

// ImageSurface — image from args url/path (full: B9).
GlassCard {
    id: root
    property string title: "Image"
    property string source: ""
    section: "Cameras"
    width: 320
    height: 200

    Image {
        anchors.fill: parent
        anchors.margins: Style.padSm
        source: root.source
        fillMode: Image.PreserveAspectFit
        asynchronous: true
    }
}
