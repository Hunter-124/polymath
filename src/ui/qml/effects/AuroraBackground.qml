import QtQuick
import QtQuick.Shapes
import Polymath

// AuroraBackground — ambient wallpaper, software-renderer safe.
// Base fill + three drifting soft blobs (Shapes RadialGradient preferred;
// stacked linear-gradient Rectangles if useShapes is false).
Item {
    id: root
    anchors.fill: parent
    z: 0

    // If capture shows RadialGradient failing under Software, flip this.
    property bool useShapes: true

    Rectangle {
        anchors.fill: parent
        color: Style.auroraBase
    }

    // --- Shape blobs --------------------------------------------------------
    component AuroraBlob: Shape {
        id: blob
        property color blobColor: Style.auroraBlobA
        property real blobSize: 800
        property real cx: 0.3
        property real cy: 0.3
        property real phase: 0
        width: blobSize
        height: blobSize
        x: root.width * cx - blobSize / 2
        y: root.height * cy - blobSize / 2
        opacity: 1.0
        preferredRendererType: Shape.CurveRenderer
        visible: root.useShapes

        ShapePath {
            fillGradient: RadialGradient {
                centerX: blob.blobSize / 2
                centerY: blob.blobSize / 2
                centerRadius: blob.blobSize / 2
                focalX: centerX
                focalY: centerY
                GradientStop { position: 0.0; color: Style.tint(blob.blobColor, Style.auroraBlobAlpha) }
                GradientStop { position: 0.55; color: Style.tint(blob.blobColor, Style.auroraBlobAlpha * 0.35) }
                GradientStop { position: 1.0; color: "transparent" }
            }
            strokeWidth: 0
            PathAngleArc {
                centerX: blob.blobSize / 2
                centerY: blob.blobSize / 2
                radiusX: blob.blobSize / 2
                radiusY: blob.blobSize / 2
                startAngle: 0
                sweepAngle: 360
            }
        }

        SequentialAnimation on scale {
            loops: Animation.Infinite
            running: Style.animationsEnabled && !Style.reduceMotion
            NumberAnimation { from: 1.0; to: 1.12; duration: Style.durAmbient / 2; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1.12; to: 1.0; duration: Style.durAmbient / 2; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on x {
            loops: Animation.Infinite
            running: Style.animationsEnabled && !Style.reduceMotion
            NumberAnimation {
                from: root.width * blob.cx - blob.blobSize / 2
                to: root.width * (blob.cx + 0.08) - blob.blobSize / 2
                duration: Style.durAmbient + blob.phase
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                from: root.width * (blob.cx + 0.08) - blob.blobSize / 2
                to: root.width * blob.cx - blob.blobSize / 2
                duration: Style.durAmbient + blob.phase
                easing.type: Easing.InOutSine
            }
        }
        SequentialAnimation on y {
            loops: Animation.Infinite
            running: Style.animationsEnabled && !Style.reduceMotion
            NumberAnimation {
                from: root.height * blob.cy - blob.blobSize / 2
                to: root.height * (blob.cy - 0.06) - blob.blobSize / 2
                duration: Style.durAmbient + blob.phase * 0.7
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                from: root.height * (blob.cy - 0.06) - blob.blobSize / 2
                to: root.height * blob.cy - blob.blobSize / 2
                duration: Style.durAmbient + blob.phase * 0.7
                easing.type: Easing.InOutSine
            }
        }
    }

    AuroraBlob {
        blobColor: Style.auroraBlobA; blobSize: 860; cx: 0.22; cy: 0.28; phase: 0
    }
    AuroraBlob {
        blobColor: Style.auroraBlobB; blobSize: 780; cx: 0.78; cy: 0.35; phase: 4000
    }
    AuroraBlob {
        blobColor: Style.auroraBlobC; blobSize: 720; cx: 0.48; cy: 0.78; phase: 8000
    }

    // --- Fallback blobs (no Shapes) ----------------------------------------
    component FallbackBlob: Rectangle {
        property color blobColor: Style.auroraBlobA
        property real blobSize: 700
        property real cx: 0.3
        property real cy: 0.3
        width: blobSize
        height: blobSize
        radius: blobSize / 2
        x: root.width * cx - blobSize / 2
        y: root.height * cy - blobSize / 2
        visible: !root.useShapes
        rotation: 25
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Style.tint(blobColor, Style.auroraBlobAlpha * 0.9) }
            GradientStop { position: 1.0; color: "transparent" }
        }
        opacity: 0.85
    }
    FallbackBlob { blobColor: Style.auroraBlobA; blobSize: 860; cx: 0.22; cy: 0.28 }
    FallbackBlob { blobColor: Style.auroraBlobB; blobSize: 780; cx: 0.78; cy: 0.35 }
    FallbackBlob { blobColor: Style.auroraBlobC; blobSize: 720; cx: 0.48; cy: 0.78 }

    // Bottom vignette
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.35
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.25) }
        }
    }
}
