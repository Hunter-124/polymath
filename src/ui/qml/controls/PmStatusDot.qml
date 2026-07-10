import QtQuick
import Polymath

// PmStatusDot — coloured status indicator with optional pulse.
// Uses `tone` — Rectangle already owns `color`.
Rectangle {
    id: root
    property color tone: Style.good
    property real size: 10
    property bool pulsing: false

    width: size
    height: size
    radius: size / 2
    color: root.tone

    SequentialAnimation on opacity {
        loops: Animation.Infinite
        running: root.pulsing && Style.animationsEnabled && !Style.reduceMotion
        NumberAnimation { from: 1.0; to: 0.3; duration: 700; easing.type: Easing.InOutSine }
        NumberAnimation { from: 0.3; to: 1.0; duration: 700; easing.type: Easing.InOutSine }
    }
}
