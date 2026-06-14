import QtQuick
import Polymath

// PersonalityAvatar — the animated "face" of a personality.
//
// Procedural by default: a themeable orb / equalizer / ring, tinted by the
// persona's accent, that breathes gently when idle and comes alive while the
// assistant is speaking.  A persona bundle can override it with a custom image
// or GIF (drop idle.gif / talking.gif beside persona.json and point the
// "avatar" block at them) — `idleSource`/`talkingSource` then take over.
//
// Everything is drawn with QtQuick primitives (no icon fonts, no Graphical
// effects module) so it renders identically on the desktop GPU path and the
// offscreen software renderer used by capture_views.
Item {
    id: root

    property string displayName: ""
    property string avatarStyle: "orb"        // orb | bars | ring | image
    property color  accent: Style.accent
    property string idleSource: ""            // file url ("" -> procedural)
    property string talkingSource: ""         // file url ("" -> procedural / idle)
    property bool   speaking: false
    property real   level: 0                  // optional 0..1 external amplitude (reserved)

    implicitWidth: 44
    implicitHeight: 44

    readonly property real  rad: Math.min(width, height) / 2
    readonly property string _style: avatarStyle.length ? avatarStyle : "orb"
    readonly property bool  useImage: _style === "image" || idleSource.length > 0
    readonly property bool  _isBars: _style === "bars"
    readonly property bool  _isRing: _style === "ring"
    readonly property bool  _isOrb:  !_isBars && !_isRing      // orb is the fallback
    readonly property string monogram:
        displayName.length ? displayName.charAt(0).toUpperCase() : ""

    function _tint(a) { return Qt.rgba(root.accent.r, root.accent.g, root.accent.b, a) }

    // ===================================================================== //
    //  Custom image / GIF override                                          //
    // ===================================================================== //
    Rectangle {
        anchors.fill: parent
        visible: root.useImage
        radius: Style.radiusSm
        color: Style.surface2
        clip: true
        border.width: 2
        border.color: root.speaking ? root.accent : root._tint(0.45)
        Behavior on border.color { ColorAnimation { duration: Style.durMed } }

        AnimatedImage {
            anchors.fill: parent
            anchors.margins: 2
            source: (root.speaking && root.talkingSource.length) ? root.talkingSource
                  : (root.idleSource.length ? root.idleSource : root.talkingSource)
            fillMode: Image.PreserveAspectCrop
            playing: true            // GIFs loop; static images simply hold a frame
            cache: false
            asynchronous: true
        }
    }

    // ===================================================================== //
    //  Procedural avatar                                                    //
    // ===================================================================== //
    Item {
        id: proc
        anchors.fill: parent
        visible: !root.useImage
        transformOrigin: Item.Center

        // Gentle breathing — faster + deeper while speaking.  Always running with
        // speaking-dependent targets, so toggling `speaking` never leaves a
        // value-source mid-flight (no stuck scale).
        SequentialAnimation on scale {
            running: !root.useImage
            loops: Animation.Infinite
            NumberAnimation {
                to: root.speaking ? 1.05 : 1.02
                duration: root.speaking ? 540 : 2400
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                to: 1.0
                duration: root.speaking ? 540 : 2400
                easing.type: Easing.InOutSine
            }
        }

        // ----- ORB ----------------------------------------------------------
        Item {
            anchors.fill: parent
            visible: root._isOrb

            // Expanding ripples while speaking (hidden + idle otherwise).
            Item {
                anchors.fill: parent
                visible: root._isOrb && root.speaking
                Repeater {
                    model: 3
                    delegate: Rectangle {
                        id: ripple
                        required property int index
                        anchors.centerIn: parent
                        width: root.rad * 1.4; height: width; radius: width / 2
                        color: "transparent"
                        border.width: 2
                        border.color: root.accent
                        opacity: 0
                        scale: 0.6
                        SequentialAnimation {
                            running: ripple.visible
                            loops: Animation.Infinite
                            PauseAnimation { duration: ripple.index * 300 }
                            ParallelAnimation {
                                NumberAnimation { target: ripple; property: "scale"
                                    from: 0.6; to: 1.7; duration: 1150; easing.type: Easing.OutCubic }
                                NumberAnimation { target: ripple; property: "opacity"
                                    from: 0.5; to: 0.0; duration: 1150; easing.type: Easing.OutCubic }
                            }
                        }
                    }
                }
            }

            // Soft halo.
            Rectangle {
                anchors.centerIn: parent
                width: root.rad * 1.7; height: width; radius: width / 2
                color: root._tint(0.12)
            }
            // Base disc + accent ring.
            Rectangle {
                anchors.centerIn: parent
                width: root.rad * 1.28; height: width; radius: width / 2
                color: root._tint(0.22)
                border.width: 1.5
                border.color: root._tint(0.6)
            }
            // Inner accent core.
            Rectangle {
                anchors.centerIn: parent
                width: root.rad * 0.92; height: width; radius: width / 2
                color: root.accent
            }
            // Highlight glint (top-left).
            Rectangle {
                width: root.rad * 0.3; height: width; radius: width / 2
                x: parent.width / 2 - root.rad * 0.46
                y: parent.height / 2 - root.rad * 0.52
                color: Qt.rgba(1, 1, 1, 0.5)
            }
            // Monogram over the core.
            Text {
                anchors.centerIn: parent
                text: root.monogram
                color: Style.accentText
                font.family: Style.fontFamily
                font.bold: true
                font.pixelSize: Math.max(8, root.rad * 0.78)
            }
        }

        // ----- BARS (equalizer) --------------------------------------------
        Item {
            anchors.fill: parent
            visible: root._isBars

            Rectangle {                       // subtle dished backdrop
                anchors.centerIn: parent
                width: root.rad * 1.85; height: width; radius: width / 2
                color: root._tint(0.12)
            }
            Row {
                anchors.centerIn: parent
                spacing: Math.max(2, root.rad * 0.16)
                Repeater {
                    model: 5
                    delegate: Rectangle {
                        required property int index
                        width: Math.max(2, root.rad * 0.2)
                        radius: width / 2
                        color: root.accent
                        anchors.verticalCenter: parent.verticalCenter
                        height: root.rad * 0.4
                        // Always running with speaking-dependent targets so the
                        // bars settle to a calm idle when speech stops.
                        SequentialAnimation on height {
                            running: root._isBars
                            loops: Animation.Infinite
                            NumberAnimation {
                                to: root.rad * (root.speaking
                                       ? (0.7 + 0.55 * (((index * 7) % 5) + 1) / 5)
                                       : 0.46)
                                duration: root.speaking ? (240 + index * 55) : 1500
                                easing.type: Easing.InOutSine
                            }
                            NumberAnimation {
                                to: root.rad * (root.speaking ? 0.3 : 0.4)
                                duration: root.speaking ? (240 + index * 55) : 1500
                                easing.type: Easing.InOutSine
                            }
                        }
                    }
                }
            }
        }

        // ----- RING (orbiting dot) -----------------------------------------
        Item {
            anchors.fill: parent
            visible: root._isRing

            Rectangle {                       // static track
                anchors.centerIn: parent
                width: root.rad * 1.55; height: width; radius: width / 2
                color: "transparent"
                border.width: 2
                border.color: root._tint(0.35)
            }
            Item {                            // rotating carrier
                id: orbit
                anchors.centerIn: parent
                width: root.rad * 1.55; height: width
                RotationAnimation on rotation {
                    running: root._isRing
                    from: 0; to: 360
                    loops: Animation.Infinite
                    duration: root.speaking ? 1500 : 6000
                }
                Rectangle {
                    width: root.rad * 0.36; height: width; radius: width / 2
                    x: parent.width / 2 - width / 2
                    y: -height * 0.12
                    color: root.accent
                }
            }
            Text {
                anchors.centerIn: parent
                text: root.monogram
                color: root.accent
                font.family: Style.fontFamily
                font.bold: true
                font.pixelSize: Math.max(8, root.rad * 0.7)
            }
        }
    }
}
