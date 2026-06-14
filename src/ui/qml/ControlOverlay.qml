import QtQuick
import QtQuick.Window
import Polymath

// ControlOverlay — a frameless, transparent, always-on-top, CLICK-THROUGH window
// that draws a glowing / fiery border around the primary screen while Hearth is
// driving the mouse and keyboard (app.controlling). Click-through
// (Qt.WindowTransparentForInput) is essential: the overlay must never intercept
// the very clicks the assistant is making. The panic-stop lives in the main
// window (it cannot sit on a click-through surface).
Window {
    id: overlay
    visible: app.controlling
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
         | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput

    x: Screen.virtualX
    y: Screen.virtualY
    width: Screen.width
    height: Screen.height

    // Layered warm borders of decreasing opacity fake a "glow" without the
    // Graphical-Effects module; each ring flickers on its own cadence for a living
    // flame feel.
    Repeater {
        model: 4
        delegate: Rectangle {
            required property int index
            anchors.fill: parent
            anchors.margins: index * 5
            color: "transparent"
            radius: 8
            border.width: 12 - index * 2
            border.color: index === 0 ? "#ffd166"
                        : index === 1 ? "#ff7b00"
                        : index === 2 ? "#ff3c00" : "#e01e00"
            opacity: 0
            SequentialAnimation on opacity {
                running: overlay.visible
                loops: Animation.Infinite
                NumberAnimation { to: 0.85 - index * 0.12; duration: 420 + index * 130; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.35 - index * 0.06; duration: 380 + index * 110; easing.type: Easing.InOutSine }
            }
        }
    }

    // A small status pill near the top edge so the user always sees WHAT it's doing.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 16
        radius: height / 2
        color: "#dd1a0d07"
        border.width: 1; border.color: "#ff7b00"
        implicitWidth: pill.implicitWidth + 30
        implicitHeight: 36
        Row {
            id: pill
            anchors.centerIn: parent
            spacing: 9
            Rectangle {
                width: 10; height: 10; radius: 5; anchors.verticalCenter: parent.verticalCenter
                color: "#ff7b00"
                SequentialAnimation on opacity {
                    running: overlay.visible; loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 480 }
                    NumberAnimation { to: 1.0; duration: 480 }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: (app.controlAction && app.controlAction.length)
                      ? ("Hearth is controlling — " + app.controlAction)
                      : "Hearth is controlling the computer"
                color: "#ffe8c2"
                font.family: Style.fontFamily; font.pixelSize: 13; font.bold: true
            }
        }
    }
}
