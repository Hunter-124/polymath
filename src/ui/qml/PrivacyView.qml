import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

Item {
    id: root
    ColumnLayout {
        anchors.fill: parent; anchors.margins: Style.pad; spacing: Style.gap

        Label {
            text: "Privacy"; color: Style.text
            font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
        }
        Label {
            text: "Everything runs locally. No telemetry. Toggle any sense off at any time."
            color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsBody
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }

        // First-run opt-in banner — senses are OFF until you allow them. Bound to
        // the live app.firstRun property; "Got it" acknowledges the flow so the
        // banner stays hidden on future launches.
        Rectangle {
            visible: app.firstRun
            Layout.fillWidth: true
            radius: Style.radiusSm
            color: Style.surface2
            border.width: 1; border.color: Style.accent
            implicitHeight: optinCol.implicitHeight + 24
            ColumnLayout {
                id: optinCol
                anchors.fill: parent; anchors.margins: 12; spacing: 6
                Label {
                    text: "First-run · choose what Hearth may sense"
                    color: Style.accent; font.family: Style.fontFamily
                    font.pixelSize: Style.fsBody; font.bold: true
                }
                Label {
                    text: "The microphone, cameras and face recognition stay OFF until you switch them on below. Nothing leaves this machine either way."
                    color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    PmButton { text: "Got it, continue"; onClicked: app.completeFirstRun() }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: Style.radius; color: Style.surface; border.color: Style.borderSoft
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 8; spacing: 0

                component Toggle: Rectangle {
                    property string keyName
                    property string caption
                    property string sub: ""
                    Layout.fillWidth: true
                    implicitHeight: 56
                    color: "transparent"
                    radius: Style.radiusSm
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 1
                            Label { text: caption; color: Style.text; font.family: Style.fontFamily; font.pixelSize: Style.fsBody }
                            Label {
                                visible: sub.length > 0; text: sub; color: Style.textFaint
                                font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                            }
                        }
                        PmSwitch {
                            checked: app.privacy(keyName)
                            onToggled: app.setPrivacy(keyName, checked)
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Style.border }
                }

                Toggle { keyName: "privacy.mic_enabled";           caption: "Microphone";          sub: "Wake word + voice commands" }
                Toggle { keyName: "privacy.ambient_transcription"; caption: "Ambient transcription"; sub: "Feeds daily summaries" }
                Toggle { keyName: "privacy.face_recognition";      caption: "Face recognition";     sub: "Identify known people on camera" }
                Toggle { keyName: "privacy.cameras_enabled";       caption: "Cameras";              sub: "ESP32-CAM live tiles + object find" }
                Toggle { keyName: "privacy.encrypt_at_rest";       caption: "Encrypt database at rest"; sub: "SQLCipher-keyed local store" }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
