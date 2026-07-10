import QtQuick
import Polymath

// GlassPanel — faux glass, software-safe workhorse (01 §2.1).
Item {
    id: root
    property real radius: Style.radiusLg
    property color tintColor: "transparent"
    property real tintAlpha: Style.glassTintAlpha
    property int elevation: 1
    property bool hovered: false
    default property alias content: contentHolder.data

    // Soft stacked shadow
    Repeater {
        model: root.elevation > 0 ? 3 : 0
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.margins: -(1 + index * 3)
            anchors.topMargin: -(2 + index * 3) + (2 + index * 4)
            radius: root.radius + (1 + index * 2)
            color: index === 0 ? Style.shadowA1
                 : index === 1 ? Style.shadowA2
                 : Style.shadowA3
        }
    }

    // Base gradient fill
    Rectangle {
        id: fill
        anchors.fill: parent
        radius: root.radius
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Qt.rgba(1, 1, 1,
                    Style.glassFillTop.a + (root.hovered ? Style.glassHoverBoost : 0))
            }
            GradientStop {
                position: 1.0
                color: Qt.rgba(1, 1, 1,
                    Style.glassFillBottom.a + (root.hovered ? Style.glassHoverBoost * 0.5 : 0))
            }
        }
        // Solid near-black underlay so glass reads over any parent
        Rectangle {
            z: -1
            anchors.fill: parent
            radius: root.radius
            color: Style.surface
            opacity: 0.72
        }
        Behavior on gradient { enabled: false }
    }

    // Section tint overlay
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        visible: root.tintColor.a > 0.001 || (String(root.tintColor) !== "#00000000"
                 && String(root.tintColor) !== "transparent")
        color: {
            var c = root.tintColor
            if (String(c) === "transparent" || c.a < 0.001)
                return "transparent"
            return Style.tint(c, root.tintAlpha)
        }
    }

    // Top inner highlight
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 1
        anchors.rightMargin: 1
        anchors.topMargin: 1
        height: 1
        radius: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Style.glassHighlight }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Hairline border
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: 1
        border.color: {
            if (root.hovered && root.tintColor.a > 0.001)
                return Style.tint(root.tintColor, 0.5)
            if (String(root.tintColor) !== "transparent" && root.tintColor.a > 0.001) {
                var t = root.tintColor
                var g = Style.glassBorder
                return Qt.rgba(
                    g.r * 0.65 + t.r * 0.35,
                    g.g * 0.65 + t.g * 0.35,
                    g.b * 0.65 + t.b * 0.35,
                    Math.max(g.a, 0.12))
            }
            return Style.glassBorder
        }
        Behavior on border.color { ColorAnimation { duration: Style.durFast } }
    }

    Item {
        id: contentHolder
        anchors.fill: parent
    }
}
