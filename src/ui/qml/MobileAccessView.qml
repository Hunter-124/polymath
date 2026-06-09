import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath
import "qrcode.js" as QR

// Settings ▸ Mobile Access — pair the phone/PWA client and toggle remote access.
//
// Pairing is local-first: the desktop mints a single-use code (5-min TTL) and
// shows it as a QR + the raw payload. The phone either scans the QR or pastes the
// payload (app/src/screens/Pair.tsx "Enter code manually"). The relay is OFF
// until the user flips "Allow remote access".
Item {
    id: root

    // The live pairing payload JSON: { relay_url, home_id, pair_code, lan_host,
    // lan_port }. Re-minted (fresh code) whenever we (re)read it.
    property string payload: ""
    property var    parsed: ({})

    function refreshPayload() {
        if (!gateway) return;
        payload = gateway.pairingPayloadJson();
        try { parsed = JSON.parse(payload); } catch (e) { parsed = ({}); }
    }

    Component.onCompleted: refreshPayload()

    Connections {
        target: gateway
        ignoreUnknownSignals: true
        function onConnectedClientsChanged(n) { /* count binds live below */ }
        function onRemoteEnabledChanged(on)   { /* switch binds live below */ }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: Style.pad; spacing: Style.gap

        Label {
            text: "Mobile Access"; color: Style.text
            font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
        }
        Label {
            text: "Pair the Hearth phone app to this machine. Pairing works over your " +
                  "home Wi-Fi with no cloud. Turn on remote access to also reach it from anywhere."
            color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsBody
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }

        // --- remote access toggle + live status ---
        Rectangle {
            Layout.fillWidth: true
            radius: Style.radius; color: Style.surface; border.color: Style.borderSoft
            implicitHeight: remoteRow.implicitHeight + 28
            RowLayout {
                id: remoteRow
                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
                anchors.topMargin: 14; anchors.bottomMargin: 14; spacing: 14
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Label {
                        text: "Allow remote access"; color: Style.text
                        font.family: Style.fontFamily; font.pixelSize: Style.fsBody; font.bold: true
                    }
                    Label {
                        text: "Dials an encrypted reverse tunnel to your relay so the phone " +
                              "works off your home network. Off keeps everything LAN-only."
                        color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
                ColumnLayout {
                    spacing: 4; Layout.alignment: Qt.AlignVCenter
                    PmSwitch {
                        Layout.alignment: Qt.AlignRight
                        checked: gateway ? gateway.remoteEnabled() : false
                        onToggled: if (gateway) gateway.setRemoteEnabled(checked)
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: (gateway ? gateway.connectedClients() : 0) + " connected"
                        color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                    }
                }
            }
        }

        // --- pairing card: QR + payload + deep link ---
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: Style.radius; color: Style.surface; border.color: Style.borderSoft

            RowLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 18

                // QR (white card so dark themes still scan).
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    width: 236; height: 236; radius: Style.radiusSm; color: "white"
                    Canvas {
                        id: qrCanvas
                        anchors.centerIn: parent
                        width: 220; height: 220
                        property string data: root.payload
                        onDataChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.reset();
                            ctx.fillStyle = "white";
                            ctx.fillRect(0, 0, width, height);
                            if (!data) return;
                            try {
                                var qr = QR.encode(data, "M");
                                var n = qr.size;
                                var quiet = 4;                 // required quiet zone
                                var total = n + quiet * 2;
                                var cell = Math.floor(width / total);
                                if (cell < 1) cell = 1;
                                var span = cell * total;
                                var ox = Math.floor((width - span) / 2);
                                var oy = Math.floor((height - span) / 2);
                                ctx.fillStyle = "black";
                                for (var r = 0; r < n; r++)
                                    for (var c = 0; c < n; c++)
                                        if (qr.isDark(r, c))
                                            ctx.fillRect(ox + (c + quiet) * cell,
                                                         oy + (r + quiet) * cell, cell, cell);
                            } catch (e) {
                                // Encoder unavailable — the payload below still pairs manually.
                            }
                        }
                    }
                }

                // Instructions + raw payload + actions.
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8

                    Label {
                        text: "Scan this in the Hearth app"
                        color: Style.text; font.family: Style.fontFamily
                        font.pixelSize: Style.fsBody; font.bold: true
                    }
                    Label {
                        text: "Open the app ▸ Scan pairing QR. No camera? Tap “Enter code " +
                              "manually” and paste the text below."
                        color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }

                    // LAN address line.
                    Label {
                        Layout.fillWidth: true
                        text: "On this network: " + (root.parsed.lan_host || "hearth.local") +
                              ":" + (root.parsed.lan_port || 8765)
                        color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                    }

                    // Selectable payload box.
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        radius: Style.radiusSm; color: Style.surface2; border.color: Style.border
                        Flickable {
                            anchors.fill: parent; anchors.margins: 10
                            contentWidth: width; contentHeight: payloadText.contentHeight
                            clip: true
                            TextEdit {
                                id: payloadText
                                width: parent.width
                                text: root.payload
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.WrapAnywhere
                                color: Style.textDim
                                selectionColor: Style.accent
                                font.family: "monospace"; font.pixelSize: Style.fsTiny
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        PmButton {
                            text: "Copy payload"
                            onClicked: { payloadText.selectAll(); payloadText.copy(); payloadText.deselect(); }
                        }
                        PmButton {
                            text: "New code"
                            flat: true
                            onClicked: { root.refreshPayload(); qrCanvas.requestPaint(); }
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: "code expires in 5 min"
                            color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                }
            }
        }
    }
}
