import QtQuick
import QtQuick.Controls.Basic
import Polymath

// PmTooltip — glass popup tooltip with optional delay.
Item {
    id: root
    property string text: ""
    property int delay: 500
    property string section: ""
    property Item target: parent
    property bool open: false

    anchors.fill: parent
    // Hover is handled by HoverHandler below (Item has no hoverEnabled).

    Timer {
        id: showTimer
        interval: root.delay
        onTriggered: root.open = true
    }
    HoverHandler {
        onHoveredChanged: {
            if (hovered) showTimer.restart()
            else { showTimer.stop(); root.open = false }
        }
    }

    Popup {
        id: pop
        visible: root.open && root.text.length > 0
        x: (root.width - width) / 2
        y: root.height + 6
        padding: 8
        closePolicy: Popup.NoAutoClose
        background: GlassPanel {
            radius: Style.radiusSm
            elevation: 2
            tintColor: root.section.length > 0 ? Style.sectionColor(root.section) : "transparent"
            tintAlpha: 0.08
        }
        contentItem: Text {
            text: root.text
            color: Style.text
            font.family: Style.fontFamily
            font.pixelSize: Style.fsSmall
        }
    }
}
