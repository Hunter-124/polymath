pragma Singleton
import QtQuick

// Style — holographic aurora design system (docs/overhaul/01_GUI_DESIGN_SYSTEM.md).
// Every view and themed control reads from here. Writable tokens are bridged from
// settings / pmEffectsEnabled in Main.qml (see 02 §Feature 1).
//
// QtQuick.Controls.Basic does not inherit container colours, so the themed
// controls under qml/controls/ paint themselves explicitly from these tokens.
QtObject {
    // --- surfaces -----------------------------------------------------------
    readonly property color bgDeep:    "#04060C"   // absolute window base behind aurora
    readonly property color bg:        "#070A12"
    readonly property color surface:   "#0E1320"
    readonly property color surface2:  "#141B2C"
    readonly property color surface3:  "#1B2439"
    readonly property color border:    "#26304A"
    readonly property color borderSoft:"#1A2236"

    // --- text ---------------------------------------------------------------
    readonly property color text:      "#E7ECFF"
    readonly property color textDim:   "#9AA6CC"
    readonly property color textFaint: "#5B6684"

    // --- accents (accent is writable — settings bridge) ---------------------
    property color accent:             "#33E1FF"
    readonly property color accentBright: "#8AF1FF"
    readonly property color accentDim: "#0E2E3B"
    readonly property color accentText: "#03121A"
    readonly property color good:      "#43E39A"
    readonly property color warn:      "#FFC24B"
    readonly property color bad:       "#FF5C78"
    readonly property color info:      "#67D3FF"
    readonly property color magenta:   "#C58CFF"

    // --- scrim behind modal popups (command palette, dialogs) ---------------
    readonly property color overlay:   "#9904060C"

    // --- glass tokens -------------------------------------------------------
    readonly property color glassFillTop:    Qt.rgba(1, 1, 1, 0.065)
    readonly property color glassFillBottom: Qt.rgba(1, 1, 1, 0.020)
    readonly property color glassBorder:     Qt.rgba(1, 1, 1, 0.10)
    readonly property color glassHighlight:  Qt.rgba(1, 1, 1, 0.22)
    readonly property real  glassTintAlpha:  0.14
    readonly property real  glassHoverBoost: 0.05
    readonly property color shadowA1: Qt.rgba(0, 0, 0, 0.20)
    readonly property color shadowA2: Qt.rgba(0, 0, 0, 0.10)
    readonly property color shadowA3: Qt.rgba(0, 0, 0, 0.05)

    // --- section hues -------------------------------------------------------
    readonly property var sectionHues: ({
        "Dashboard": "#33E1FF", "Chat": "#4C8CFF", "Cameras": "#2BD9C6",
        "Tasks": "#FFB23E", "Timeline": "#9B6BFF", "Shopping": "#46E08A",
        "Lab": "#43E39A", "Personalities": "#FF6BD0", "Models": "#6D7BFF",
        "Privacy": "#FF5C6E", "Mobile Access": "#4FB8FF", "Agents": "#7FE0FF",
        "Settings": "#A8B4D8"
    })
    function sectionColor(name) {
        return sectionHues[name] !== undefined ? sectionHues[name] : accent
    }
    function sectionGlow(name, a) {
        var c = sectionColor(name)
        return Qt.rgba(c.r, c.g, c.b, a === undefined ? 0.35 : a)
    }
    function tint(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    // --- aurora ------------------------------------------------------------
    readonly property color auroraBase:   "#04060C"
    readonly property color auroraBlobA:  "#33E1FF"
    readonly property color auroraBlobB:  "#6D7BFF"
    readonly property color auroraBlobC:  "#2BD9C6"
    readonly property real  auroraBlobAlpha: 0.22

    // --- shape & rhythm -----------------------------------------------------
    readonly property int   radiusLg:  16
    readonly property int   radius:    12
    readonly property int   radiusSm:  8
    readonly property int   radiusXs:  6
    readonly property int   radiusPill: 999
    readonly property int   gap:       12
    readonly property int   gapLg:     16
    readonly property int   gapSm:     8
    readonly property int   pad:       24
    readonly property int   padSm:     16
    readonly property int   controlH:  36
    readonly property int   controlHsm: 30

    // --- navigation rail (existing shell bindings) --------------------------
    readonly property int   railWidth:          216
    readonly property int   railWidthCollapsed: 64

    // --- type ---------------------------------------------------------------
    property string fontFamily: ""
    property real   fontScale: 1.0
    readonly property int   fsDisplay: 30
    readonly property int   fsTitle:   24
    readonly property int   fsHeading: 18
    readonly property int   fsBody:    14
    readonly property int   fsSmall:   12
    readonly property int   fsTiny:    11
    readonly property real  letterSpaceWide: 2

    // --- motion -------------------------------------------------------------
    // Spec names (01) + legacy aliases used by the current shell (durMed, ease*).
    readonly property int   durFast: 120
    readonly property int   durBase: 200
    readonly property int   durMed:  160
    readonly property int   durSlow: 320
    readonly property int   durAmbient: 26000
    readonly property int   easeStandard:   Easing.OutCubic
    readonly property int   easeEmphasized: Easing.OutBack
    property bool animationsEnabled: true
    property bool reduceMotion: false

    // --- elevation / icons --------------------------------------------------
    readonly property int   iconSm: 16
    readonly property int   iconMd: 20
    readonly property int   iconLg: 24
    readonly property int   iconXl: 28

    // --- effects (writable; Main bridges settings && pmEffectsEnabled) ------
    property bool effectsEnabled: true
    property real effectsIntensity: 0.6
}
