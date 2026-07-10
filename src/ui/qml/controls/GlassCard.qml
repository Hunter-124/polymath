import QtQuick
import Polymath

// GlassCard — GlassPanel preset for view cards (01 §2.2).
GlassPanel {
    id: root
    property string section: ""
    radius: Style.radius
    elevation: 1
    tintColor: section.length > 0 ? Style.sectionColor(section) : "transparent"
    tintAlpha: 0.10
}
