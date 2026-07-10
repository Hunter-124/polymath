import QtQuick
import QtQuick.Layouts
import Polymath

// SettingsSection — titled glass block with content slot (02 §F1).
Item {
    id: root
    property string title: ""
    property string sectionKey: ""   // deep-link id (appearance, audio, …)
    property string section: "Settings"
    default property alias content: body.data

    implicitHeight: card.implicitHeight
    width: parent ? parent.width : 400

    GlassCard {
        id: card
        anchors.left: parent.left
        anchors.right: parent.right
        section: root.section
        implicitHeight: col.implicitHeight + Style.padSm * 2

        ColumnLayout {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Style.padSm
            spacing: Style.gapSm

            RowLayout {
                Layout.fillWidth: true
                spacing: Style.gapSm
                Text {
                    text: root.title
                    color: Style.text
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsHeading
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    width: 24
                    height: 3
                    radius: 1.5
                    color: Style.sectionColor(root.section)
                }
            }

            Item {
                id: body
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
            }
        }
    }
}
