import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 12

        Label { text: "Privacy & Settings"; color: "#c0caf5"; font.pixelSize: 24; font.bold: true }
        Label {
            text: "Everything runs locally. No telemetry. Toggle any sense off at any time."
            color: "#565f89"; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }

        component Toggle: RowLayout {
            property string keyName
            property string caption
            Layout.fillWidth: true
            Label { text: caption; color: "#c0caf5"; Layout.fillWidth: true }
            Switch {
                checked: app.privacy(keyName)
                onToggled: app.setPrivacy(keyName, checked)
            }
        }

        Toggle { keyName: "privacy.mic_enabled";           caption: "Microphone" }
        Toggle { keyName: "privacy.ambient_transcription"; caption: "Ambient transcription (daily summaries)" }
        Toggle { keyName: "privacy.face_recognition";      caption: "Face recognition (identify users)" }
        Toggle { keyName: "privacy.cameras_enabled";       caption: "Cameras" }
        Toggle { keyName: "privacy.encrypt_at_rest";       caption: "Encrypt database at rest" }

        Item { Layout.fillHeight: true }
    }
}
