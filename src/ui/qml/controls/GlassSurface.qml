import QtQuick
import Polymath

// GlassSurface — hero glass surface.
// A1 ships faux-glass only (software-safe; matches capture path).
// Live MultiEffect blur lands when effectsEnabled + Qt Quick Effects are
// available (wave BV / D1 can reintroduce without API break).
Item {
    id: root
    property Item sourceItem: null
    property real radius: Style.radiusLg
    property real blur: 1.0
    property string section: ""
    property int elevation: 2
    default property alias content: contentHolder.data

    GlassPanel {
        anchors.fill: parent
        radius: root.radius
        elevation: root.elevation
        tintColor: root.section.length > 0 ? Style.sectionColor(root.section) : "transparent"
        tintAlpha: 0.10
    }

    Item {
        id: contentHolder
        anchors.fill: parent
        z: 2
    }
}
