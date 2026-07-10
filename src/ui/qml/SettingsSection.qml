import QtQuick
import QtQuick.Layouts
import Polymath

// SettingsSection — section header + content slot (full polish in B6).
Item {
    id: root
    property string title: ""
    property string section: "Settings"
    default property alias content: body.data
    implicitHeight: col.implicitHeight
    width: parent ? parent.width : 400

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Style.gapSm
        Text {
            text: root.title
            color: Style.text
            font.family: Style.fontFamily
            font.pixelSize: Style.fsHeading
            font.bold: true
        }
        Rectangle {
            width: 24; height: 2; radius: 1
            color: Style.sectionColor(root.section)
        }
        Item {
            id: body
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
        }
    }
}
