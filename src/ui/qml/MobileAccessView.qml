import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath
import "qrcode.js" as QR

// Mobile Access — sky glass cards; white QR preserved (01 §5.10).
//
// Pairing is local-first: the desktop mints a single-use code (5-min TTL) and
// shows it as a QR + the raw payload. The phone either scans the QR or pastes the
// payload. The relay is OFF until the user flips "Allow remote access".
Item {
    id: root

    property string payload: ""
    property var    parsed: ({})

    function refreshPayload() {
        if (!gateway) return
        payload = gateway.pairingPayloadJson()
        try { parsed = JSON.parse(payload) } catch (e) { parsed = ({}) }
    }

    Component.onCompleted: refreshPayload()

    Connections {
        target: gateway
        ignoreUnknownSignals: true
        function onConnectedClientsChanged(n) { /* count binds live below */ }
        function onRemoteEnabledChanged(on)   { /* switch binds live below */ }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Mobile Access"
            section: "Mobile Access"
            subtitle: "Pair the Polymath phone app to this machine. Pairing works over your home Wi-Fi with no cloud. Turn on remote access to also reach it from anywhere."
        }

        // --- remote access toggle + live status ---
        GlassCard {
            Layout.fillWidth: true
            Layout.preferredHeight: remoteRow.implicitHeight + 28
            section: "Mobile Access"
            RowLayout {
                id: remoteRow
                anchors.fill: parent
                anchors.leftMargin: Style.padSm
                anchors.rightMargin: Style.padSm
                anchors.topMargin: Style.padSm
                anchors.bottomMargin: Style.padSm
                spacing: Style.gap
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: "Allow remote access"
                        color: Style.text
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "Dials an encrypted reverse tunnel to your relay so the phone "
                              + "works off your home network. Off keeps everything LAN-only."
                        color: Style.textFaint
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsTiny
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
                ColumnLayout {
                    spacing: 4
                    Layout.alignment: Qt.AlignVCenter
                    PmSwitch {
                        Layout.alignment: Qt.AlignRight
                        tone: Style.sectionColor("Mobile Access")
                        checked: gateway ? gateway.remoteEnabled() : false
                        onToggled: if (gateway) gateway.setRemoteEnabled(checked)
                    }
                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: (gateway ? gateway.connectedClients() : 0) + " connected"
                        color: Style.textFaint
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsTiny
                    }
                }
            }
        }

        // --- pairing card: QR + payload + deep link ---
        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Mobile Access"

            RowLayout {
                anchors.fill: parent
                anchors.margins: Style.padSm
                spacing: Style.gapLg

                // QR (white card so dark themes still scan) — keep white exactly.
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    width: 236
                    height: 236
                    radius: Style.radiusSm
                    color: "white"
                    Canvas {
                        id: qrCanvas
                        anchors.centerIn: parent
                        width: 220
                        height: 220
                        property string data: root.payload
                        onDataChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            ctx.fillStyle = "white"
                            ctx.fillRect(0, 0, width, height)
                            if (!data) return
                            try {
                                var qr = QR.encode(data, "M")
                                var n = qr.size
                                var quiet = 4
                                var total = n + quiet * 2
                                var cell = Math.floor(width / total)
                                if (cell < 1) cell = 1
                                var span = cell * total
                                var ox = Math.floor((width - span) / 2)
                                var oy = Math.floor((height - span) / 2)
                                ctx.fillStyle = "black"
                                for (var r = 0; r < n; r++)
                                    for (var c = 0; c < n; c++)
                                        if (qr.isDark(r, c))
                                            ctx.fillRect(ox + (c + quiet) * cell,
                                                         oy + (r + quiet) * cell, cell, cell)
                            } catch (e) {
                                // Encoder unavailable — the payload below still pairs manually.
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Style.gapSm

                    Label {
                        text: "Scan this in the Polymath app"
                        color: Style.text
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsBody
                        font.bold: true
                    }
                    Label {
                        text: "Open the app > Scan pairing QR. No camera? Tap \"Enter code "
                              + "manually\" and paste the text below."
                        color: Style.textFaint
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsTiny
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "On this network: " + (root.parsed.lan_host || "polymath.local")
                              + ":" + (root.parsed.lan_port || 8765)
                        color: Style.textDim
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsSmall
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Style.radiusSm
                        color: Style.surface2
                        border.color: Style.border
                        border.width: 1
                        Flickable {
                            anchors.fill: parent
                            anchors.margins: Style.gapSm
                            contentWidth: width
                            contentHeight: payloadText.contentHeight
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
                                font.family: "monospace"
                                font.pixelSize: Style.fsTiny
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Style.gapSm
                        PmButton {
                            text: "Copy payload"
                            onClicked: {
                                payloadText.selectAll()
                                payloadText.copy()
                                payloadText.deselect()
                            }
                        }
                        PmButton {
                            text: "New code"
                            flat: true
                            onClicked: {
                                root.refreshPayload()
                                qrCanvas.requestPaint()
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: "code expires in 5 min"
                            color: Style.textFaint
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsTiny
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                }
            }
        }
    }
}
