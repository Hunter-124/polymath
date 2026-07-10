import QtQuick
import QtQuick.Layouts
import Polymath

// PmSectionHeader — title + subtitle + optional right-side actions.
Item {
    id: root
    property string title: ""
    property string subtitle: ""
    property string section: ""
    default property alias actions: actionRow.data

    implicitHeight: col.implicitHeight
    implicitWidth: parent ? parent.width : 400

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gap
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: root.title
                    color: Style.text
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsTitle
                    font.bold: true
                }
                Rectangle {
                    width: 28
                    height: 3
                    radius: 1.5
                    color: root.section.length > 0 ? Style.sectionColor(root.section) : Style.accent
                }
                Text {
                    visible: root.subtitle.length > 0
                    text: root.subtitle
                    color: Style.textFaint
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsSmall
                }
            }
            Row {
                id: actionRow
                spacing: Style.gapSm
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }
}
