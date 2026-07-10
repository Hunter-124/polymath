import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Personalities — magenta glass delegates (01 §5.7).
Item {
    id: root
    property var people: app.personalities()
    Component.onCompleted: people = app.personalities()

    Connections {
        target: app
        function onActivePersonalityChanged() { root.people = app.personalities() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Personalities"
            section: "Personalities"
            subtitle: "Modular historical minds. Drop a folder into  personalities/<name>/persona.json  to add one."
        }

        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Personalities"

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: Style.gapSm
                clip: true
                spacing: Style.gapSm
                model: root.people
                ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Personalities") }

                delegate: PmItemDelegate {
                    id: pd
                    required property string modelData
                    width: ListView.view ? ListView.view.width : 0
                    text: pd.modelData
                    tone: Style.sectionColor("Personalities")
                    highlighted: pd.modelData === app.activePersonality
                    onClicked: app.setPersonality(pd.modelData)

                    PmBadge {
                        anchors.right: parent.right
                        anchors.rightMargin: Style.padSm
                        anchors.verticalCenter: parent.verticalCenter
                        visible: pd.modelData === app.activePersonality
                        text: "active"
                        tone: Style.good
                        filled: true
                    }
                }

                EmptyState {
                    anchors.fill: parent
                    visible: list.count === 0
                    glyph: "o"
                    iconName: "person"
                    glyphColor: Style.sectionColor("Personalities")
                    title: "No personalities found"
                    hint: "Add a persona bundle under  personalities/<name>/persona.json  and it appears here, hot-loaded."
                }
            }
        }
    }
}
