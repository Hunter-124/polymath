import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// SettingsView — the hub that keeps rarely-used pages out of the main rail.
// Personalities, Models, Privacy and Mobile Access render inside it, picked by
// a slim sub-nav on the left.  Nav.settingsSection is the single source of
// truth, so the command palette can deep-link straight to a section.
Item {
    id: root

    readonly property var sections: [
        { name: "Personalities", icon: "person", src: "PersonalitiesView.qml" },
        { name: "Models",        icon: "chip",   src: "ModelManagerView.qml" },
        { name: "Privacy",       icon: "shield", src: "PrivacyView.qml" },
        { name: "Mobile Access", icon: "phone",  src: "MobileAccessView.qml" }
    ]

    readonly property int sectionIndex: {
        for (var i = 0; i < sections.length; ++i)
            if (sections[i].name === Nav.settingsSection) return i
        return 0
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- sub-nav ---
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 176
            color: "transparent"
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Style.border }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 4

                Label {
                    text: "SETTINGS"
                    color: Style.textFaint
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsTiny; font.bold: true; font.letterSpacing: 1.5
                    Layout.bottomMargin: 8
                    Layout.topMargin: 10
                    Layout.leftMargin: 4
                }

                Repeater {
                    model: root.sections
                    delegate: AbstractButton {
                        id: item
                        required property var modelData
                        required property int index
                        readonly property bool current: root.sectionIndex === index
                        Layout.fillWidth: true
                        implicitHeight: 32
                        onClicked: Nav.settingsSection = modelData.name

                        background: Rectangle {
                            radius: Style.radiusSm
                            color: item.current ? Style.accentDim
                                 : item.hovered ? Style.surface2 : "transparent"
                            Behavior on color { ColorAnimation { duration: Style.durFast } }
                        }
                        contentItem: RowLayout {
                            spacing: 9
                            PmIcon {
                                Layout.leftMargin: 8
                                width: 15; height: 15
                                name: item.modelData.icon
                                color: item.current ? Style.accent
                                     : item.hovered ? Style.text : Style.textDim
                            }
                            Label {
                                text: item.modelData.name
                                color: item.current ? Style.accent
                                     : item.hovered ? Style.text : Style.textDim
                                font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        // --- section content (lazy; warm after first visit) ---
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.sectionIndex
            Repeater {
                model: root.sections
                delegate: Loader {
                    required property var modelData
                    required property int index
                    property bool everActive: false
                    source: modelData.src
                    active: everActive || root.sectionIndex === index
                    onActiveChanged: if (active) everActive = true
                }
            }
        }
    }
}
