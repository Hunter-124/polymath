import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Privacy — rose glass toggles (01 §5.9).
Item {
    id: root
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Privacy & Settings"
            section: "Privacy"
            subtitle: "Everything runs locally. No telemetry. Toggle any sense off at any time."
        }

        // First-run opt-in banner
        GlassCard {
            visible: app.firstRun
            Layout.fillWidth: true
            Layout.preferredHeight: optinCol.implicitHeight + Style.pad
            section: "Privacy"

            ColumnLayout {
                id: optinCol
                anchors.fill: parent
                anchors.margins: Style.padSm
                spacing: 6
                Text {
                    text: "First-run · choose what Polymath may sense"
                    color: Style.sectionColor("Privacy")
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsBody
                    font.bold: true
                }
                Text {
                    text: "The microphone, cameras and face recognition stay OFF until you switch them on below. Nothing leaves this machine either way."
                    color: Style.textDim
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    PmButton {
                        text: "Got it, continue"
                        accent: true
                        tone: Style.sectionColor("Privacy")
                        onClicked: app.completeFirstRun()
                    }
                }
            }
        }

        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Privacy"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Style.gapSm
                spacing: 0

                component Toggle: Rectangle {
                    property string keyName
                    property string caption
                    property string sub: ""
                    Layout.fillWidth: true
                    implicitHeight: 56
                    color: rowHover.hovered
                           ? Style.tint(Style.sectionColor("Privacy"), 0.08)
                           : "transparent"
                    radius: Style.radiusSm
                    HoverHandler { id: rowHover }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Style.gap
                        anchors.rightMargin: Style.gap
                        spacing: Style.gap
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label {
                                text: caption
                                color: Style.text
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsBody
                            }
                            Label {
                                visible: sub.length > 0
                                text: sub
                                color: Style.textFaint
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny
                            }
                        }
                        PmSwitch {
                            tone: Style.sectionColor("Privacy")
                            checked: app.privacy(keyName)
                            onToggled: app.setPrivacy(keyName, checked)
                        }
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Style.borderSoft
                    }
                }

                Toggle {
                    keyName: "privacy.mic_enabled"
                    caption: "Microphone"
                    sub: "Wake word + voice commands"
                }
                Toggle {
                    keyName: "privacy.ambient_transcription"
                    caption: "Ambient transcription"
                    sub: "Feeds daily summaries"
                }
                Toggle {
                    keyName: "privacy.face_recognition"
                    caption: "Face recognition"
                    sub: "Identify known people on camera"
                }
                Toggle {
                    keyName: "privacy.cameras_enabled"
                    caption: "Cameras"
                    sub: "ESP32-CAM live tiles + object find"
                }
                Toggle {
                    keyName: "privacy.screen_capture"
                    caption: "Screen capture"
                    sub: "AI can glance at your screen when asked"
                }
                Toggle {
                    keyName: "privacy.encrypt_at_rest"
                    caption: "Encrypt database at rest"
                    sub: "SQLCipher-keyed local store"
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
