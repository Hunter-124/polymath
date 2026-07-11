import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// ConfirmDialog — C1 glass modal for SafetyPolicy ConfirmRequest.
// Hosted from Main.qml; answers via app.respondConfirm(id, approved, alwaysAllow).
PmDialog {
    id: root
    title: qsTr("Needs your approval")
    section: "Settings"
    // Slightly wider than the default PmDialog so args JSON fits.
    width: Math.min(Style.pad * 22,
                    (Overlay.overlay ? Overlay.overlay.width : 640) - Style.pad * 2)

    property string confirmId: ""
    property string toolName: ""
    property string summary: ""
    property string argsPreview: ""
    property string reason: ""

    function openConfirm(id, tool, summaryText, args, reasonText) {
        confirmId = id || ""
        toolName = tool || ""
        summary = summaryText || ""
        argsPreview = args || ""
        reason = reasonText || ""
        open()
    }

    function settle(approved, alwaysAllow) {
        if (confirmId.length === 0) {
            close()
            return
        }
        var id = confirmId
        confirmId = ""
        if (typeof app !== "undefined" && app && typeof app.respondConfirm === "function")
            app.respondConfirm(id, approved, !!alwaysAllow)
        close()
    }

    // Body content (PmDialog default property → body)
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Style.gap

        // Tool chip
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gapSm
            PmStatusDot {
                tone: Style.warn
                pulsing: true
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                text: root.toolName.length > 0 ? root.toolName : qsTr("tool")
                color: Style.accent
                font.family: Style.fontFamily
                font.pixelSize: Style.fsBody
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.summary
            color: Style.text
            font.family: Style.fontFamily
            font.pixelSize: Style.fsBody
            wrapMode: Text.WordWrap
            visible: root.summary.length > 0
        }

        Text {
            Layout.fillWidth: true
            text: root.reason
            color: Style.textDim
            font.family: Style.fontFamily
            font.pixelSize: Style.fsSmall
            wrapMode: Text.WordWrap
            visible: root.reason.length > 0
        }

        // Args preview card
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(argsFlick.contentHeight + Style.gapSm * 2,
                                             Style.controlH * 5)
            visible: root.argsPreview.length > 0
            radius: Style.radiusSm
            color: Style.surface2
            border.width: 1
            border.color: Style.glassBorder
            clip: true

            Flickable {
                id: argsFlick
                anchors.fill: parent
                anchors.margins: Style.gapSm
                contentWidth: width
                contentHeight: argsText.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: PmScrollBar { }

                Text {
                    id: argsText
                    width: parent.width
                    text: root.argsPreview
                    color: Style.textDim
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsTiny
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }

    // Footer buttons (PmDialog footer alias)
    footer: [
        PmButton {
            text: qsTr("Deny")
            flat: true
            tone: Style.bad
            onClicked: root.settle(false, false)
        },
        PmButton {
            text: qsTr("Always allow this tool")
            flat: true
            tone: Style.warn
            onClicked: root.settle(true, true)
        },
        PmButton {
            text: qsTr("Approve")
            accent: true
            tone: Style.good
            onClicked: root.settle(true, false)
        }
    ]
}
