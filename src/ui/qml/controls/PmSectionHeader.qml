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

    // Height from content only. Do NOT bind implicitWidth to parent.width —
    // that creates a Layouts cycle (parent sizes children from implicit sizes).
    implicitHeight: col.implicitHeight
    implicitWidth: col.implicitWidth

    ColumnLayout {
        id: col
        width: root.width > 0 ? root.width : col.implicitWidth
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
