pragma Singleton
import QtQuick

// Style — the single source of truth for the Hearth palette, spacing, radii
// and type ramp.  Every view and themed control reads from here so the dark
// "Tokyo Night" look stays cohesive and is tweakable in one place.
//
// QtQuick.Controls.Basic does not inherit container colours, so the themed
// controls under qml/controls/ paint themselves explicitly from these tokens.
QtObject {
    // --- surfaces -----------------------------------------------------------
    readonly property color bg:        "#0f1115"   // window background
    readonly property color surface:   "#171a21"   // cards / nav rail
    readonly property color surface2:  "#1f2335"   // raised rows / inputs
    readonly property color surface3:  "#24283b"   // hovered / assistant bubble
    readonly property color border:    "#2a2f41"   // hairline separators
    readonly property color borderSoft:"#24283b"

    // --- text ---------------------------------------------------------------
    readonly property color text:      "#c0caf5"   // primary
    readonly property color textDim:   "#8a93b8"   // secondary
    readonly property color textFaint: "#565f89"   // tertiary / placeholders

    // --- accents ------------------------------------------------------------
    readonly property color accent:    "#7aa2f7"   // primary blue
    readonly property color accentDim: "#3b4261"   // accent at rest / pressed bg
    readonly property color good:      "#9ece6a"   // live / done / success
    readonly property color warn:      "#e0af68"   // queued / caution
    readonly property color bad:       "#f7768e"   // error
    readonly property color info:      "#7dcfff"   // transcript / info
    readonly property color magenta:   "#bb9af7"   // event

    // --- text drawn over a filled accent surface ----------------------------
    // (Not named on*, which QML would parse as a signal handler.)
    readonly property color accentText: "#0f1115"

    // --- shape & rhythm -----------------------------------------------------
    readonly property int   radius:    10
    readonly property int   radiusSm:  8
    readonly property int   radiusXs:  6
    readonly property int   gap:       12
    readonly property int   gapLg:     16
    readonly property int   pad:       24
    readonly property int   controlH:  36

    // --- type ---------------------------------------------------------------
    // Set by Main.qml's FontLoader once Inter is registered; falls back to the
    // platform default so the app is still legible if the resource is missing.
    property string fontFamily: ""
    readonly property int   fsTitle:   24
    readonly property int   fsHeading:  18
    readonly property int   fsBody:     14
    readonly property int   fsSmall:    12
    readonly property int   fsTiny:     11
}
