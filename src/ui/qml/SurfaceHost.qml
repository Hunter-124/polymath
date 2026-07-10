import QtQuick
import Polymath

// SurfaceHost — placeholder overlay (full impl: B9).
// Listens for app.surfaceRequested once C1 wires it into Main.
Item {
    id: root
    anchors.fill: parent
    z: 40
    // Live surface model + Loader map arrive in B9.
}
