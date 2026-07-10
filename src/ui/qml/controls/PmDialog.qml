import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// PmDialog — glass modal with title, content, footer.
Popup {
    id: root
    property string title: ""
    property string section: ""
    default property alias content: body.data
    property alias footer: footerRow.data

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(480, (Overlay.overlay ? Overlay.overlay.width : 640) - 48)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }

    background: GlassPanel {
        radius: Style.radiusLg
        elevation: 3
        tintColor: root.section.length > 0 ? Style.sectionColor(root.section) : "transparent"
        tintAlpha: 0.08
    }

    contentItem: ColumnLayout {
        spacing: 0
        // Header
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Style.padSm
            Text {
                Layout.fillWidth: true
                text: root.title
                color: Style.text
                font.family: Style.fontFamily
                font.pixelSize: Style.fsHeading
                font.bold: true
            }
            PmToolButton {
                iconName: "x"
                destructive: true
                onClicked: root.close()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Style.glassBorder
        }
        Item {
            id: body
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            Layout.margins: Style.padSm
        }
        Row {
            id: footerRow
            Layout.alignment: Qt.AlignRight
            Layout.margins: Style.padSm
            spacing: Style.gapSm
        }
    }
}
