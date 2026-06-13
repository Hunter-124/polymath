import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// LabView — the desktop cockpit for the interactive lab assistant (v0.2).
// Left: a live readout of every connected instrument (InstrumentModel, updated by
// EventBus::instrumentReading). Right: guided lab sessions (LabModel) with their
// verified-step progress, expandable to the per-step plan. A banner surfaces the
// agent's current step live (LabModel.liveStep*). Power-user surface; standard
// users simply never open it.
Item {
    id: root
    Component.onCompleted: app.refreshLab()

    function statusColor(s) {
        switch (s) {
            case "active":   return Style.accent
            case "done":     return Style.good
            case "canceled": return Style.textFaint
            default:         return Style.warn
        }
    }
    function liveTone(s) {
        if (s === "out_of_range") return Style.bad
        if (s === "verified" || s === "done") return Style.good
        return Style.accent
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: Style.pad; spacing: Style.gap

        // --- header -------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Label {
                text: "Lab"; color: Style.text
                font.family: Style.fontFamily; font.pixelSize: Style.fsTitle; font.bold: true
            }
            Rectangle {
                visible: labModel.activeCount > 0
                radius: height / 2
                color: Qt.rgba(Style.accent.r, Style.accent.g, Style.accent.b, 0.14)
                implicitHeight: 24; implicitWidth: acRow.implicitWidth + 18
                RowLayout {
                    id: acRow; anchors.centerIn: parent; spacing: 5
                    Rectangle { width: 7; height: 7; radius: 3.5; color: Style.accent }
                    Label {
                        text: labModel.activeCount + " active"
                        color: Style.accent
                        font.family: Style.fontFamily; font.pixelSize: Style.fsTiny; font.bold: true
                    }
                }
            }
            Item { Layout.fillWidth: true }
            PmIconButton { glyph: "refresh"; tip: "Refresh"; onClicked: app.refreshLab() }
        }

        Label {
            text: "Run a guided experiment by voice — say \"start a lab session\". Hearth walks each step, "
                + "reads connected instruments or asks you for the value, verifies it against the expected range, "
                + "and writes the report when you're done."
            color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
            wrapMode: Text.WordWrap; Layout.fillWidth: true
        }

        // --- live current-step banner ------------------------------------
        Rectangle {
            Layout.fillWidth: true
            visible: labModel.liveSessionId > 0 && labModel.livePrompt.length > 0
            radius: Style.radiusSm
            implicitHeight: bannerRow.implicitHeight + 18
            color: Qt.rgba(root.liveTone(labModel.liveStatus).r, root.liveTone(labModel.liveStatus).g,
                           root.liveTone(labModel.liveStatus).b, 0.12)
            border.width: 1; border.color: root.liveTone(labModel.liveStatus)
            RowLayout {
                id: bannerRow
                anchors.fill: parent; anchors.margins: 10; spacing: 12
                Rectangle {
                    width: 10; height: 10; radius: 5; Layout.alignment: Qt.AlignTop; Layout.topMargin: 3
                    color: root.liveTone(labModel.liveStatus)
                    SequentialAnimation on opacity {
                        running: labModel.liveStatus !== "done"; loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: 650 }
                        NumberAnimation { to: 1.0; duration: 650 }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Label {
                        text: labModel.liveStatus === "out_of_range" ? "Out of range — re-measure" : "Current step"
                        color: root.liveTone(labModel.liveStatus)
                        font.family: Style.fontFamily; font.pixelSize: Style.fsTiny; font.bold: true
                    }
                    Label {
                        text: labModel.livePrompt; color: Style.text
                        font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
            }
        }

        // --- body: instruments | sessions --------------------------------
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: Style.gap

            // LEFT — live instruments
            Rectangle {
                Layout.preferredWidth: Math.round(root.width * 0.34)
                Layout.minimumWidth: 240
                Layout.fillHeight: true
                radius: Style.radius; color: Style.surface; border.color: Style.borderSoft
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Label {
                        text: "Live instruments"; color: Style.textDim
                        font.family: Style.fontFamily; font.pixelSize: Style.fsSmall; font.bold: true
                    }
                    ListView {
                        id: instList
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; spacing: 6; model: instrumentModel

                        delegate: Rectangle {
                            required property string name
                            required property string unit
                            required property string deviceClass
                            required property double value
                            required property bool   inRange
                            required property bool   hasReading
                            width: instList.view ? instList.view.width : instList.width
                            height: 58; radius: Style.radiusSm; color: Style.surface2

                            RowLayout {
                                anchors.fill: parent; anchors.margins: 10; spacing: 10
                                Rectangle {
                                    width: 9; height: 9; radius: 4.5; Layout.alignment: Qt.AlignVCenter
                                    color: !hasReading ? Style.textFaint : (inRange ? Style.good : Style.bad)
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 1
                                    Label {
                                        text: name; color: Style.text; elide: Text.ElideRight
                                        font.family: Style.fontFamily; font.pixelSize: Style.fsBody; Layout.fillWidth: true
                                    }
                                    Label {
                                        text: deviceClass; color: Style.textFaint
                                        font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                                    }
                                }
                                Label {
                                    text: hasReading ? (value.toFixed(2) + (unit.length ? " " + unit : "")) : "—"
                                    color: !hasReading ? Style.textFaint : (inRange ? Style.text : Style.bad)
                                    font.family: Style.fontFamily; font.pixelSize: Style.fsHeading; font.bold: true
                                }
                            }
                        }

                        EmptyState {
                            anchors.fill: parent
                            visible: instList.count === 0
                            glyph: "◇"
                            title: "No instruments connected"
                            hint: "Hearth Measurement Modules announce themselves on the device fabric. Readings appear here live."
                        }
                    }
                }
            }

            // RIGHT — lab sessions
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                radius: Style.radius; color: Style.surface; border.color: Style.borderSoft
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 8
                    Label {
                        text: "Sessions"; color: Style.textDim
                        font.family: Style.fontFamily; font.pixelSize: Style.fsSmall; font.bold: true
                    }
                    ListView {
                        id: sessList
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; spacing: 6; model: labModel

                        delegate: Rectangle {
                            id: srow
                            required property int    index
                            required property var     sessionId
                            required property string  title
                            required property string  objective
                            required property string  status
                            required property int     stepCount
                            required property int     verifiedCount
                            required property var     reportDocId
                            property bool open: false
                            property var  stepRows: []
                            width: sessList.view ? sessList.view.width : sessList.width
                            implicitHeight: scol.implicitHeight + 20
                            radius: Style.radiusSm; color: Style.surface2

                            ColumnLayout {
                                id: scol
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.top: parent.top; anchors.margins: 10; spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    PmIcon {
                                        width: 14; height: 14
                                        name: srow.open ? "chevron-down" : "chevron-right"; color: Style.textDim
                                    }
                                    Label {
                                        text: srow.title; color: Style.text; elide: Text.ElideRight
                                        font.family: Style.fontFamily; font.pixelSize: Style.fsBody; font.bold: true
                                        Layout.fillWidth: true
                                    }
                                    Rectangle {
                                        radius: Style.radiusXs
                                        color: Qt.rgba(root.statusColor(srow.status).r, root.statusColor(srow.status).g,
                                                       root.statusColor(srow.status).b, 0.16)
                                        implicitWidth: stp.implicitWidth + 14; implicitHeight: stp.implicitHeight + 5
                                        Label {
                                            id: stp; anchors.centerIn: parent
                                            text: srow.status; color: root.statusColor(srow.status)
                                            font.family: Style.fontFamily; font.pixelSize: Style.fsTiny; font.bold: true
                                        }
                                    }
                                }

                                Label {
                                    visible: srow.objective.length > 0
                                    text: srow.objective; color: Style.textDim
                                    font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }

                                // progress: verified / total steps
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 8; visible: srow.stepCount > 0
                                    Rectangle {
                                        Layout.fillWidth: true; height: 5; radius: 2.5; color: Style.surface3
                                        Rectangle {
                                            height: parent.height; radius: 2.5; color: Style.good
                                            width: parent.width * (srow.stepCount > 0 ? srow.verifiedCount / srow.stepCount : 0)
                                            Behavior on width { NumberAnimation { duration: Style.durMed } }
                                        }
                                    }
                                    Label {
                                        text: srow.verifiedCount + "/" + srow.stepCount + " verified"
                                        color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                                    }
                                }

                                // expanded step list
                                ColumnLayout {
                                    Layout.fillWidth: true; Layout.topMargin: 2; spacing: 4
                                    visible: srow.open
                                    Repeater {
                                        model: srow.open ? srow.stepRows : []
                                        delegate: RowLayout {
                                            required property var modelData
                                            Layout.fillWidth: true; spacing: 8
                                            Rectangle {
                                                width: 7; height: 7; radius: 3.5; Layout.alignment: Qt.AlignTop; Layout.topMargin: 5
                                                color: modelData.verified ? Style.good : Style.textFaint
                                            }
                                            Label {
                                                text: modelData.stepNo + ". " + modelData.prompt
                                                color: Style.textDim; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                                                wrapMode: Text.WordWrap; Layout.fillWidth: true
                                            }
                                            Label {
                                                visible: modelData.measuredValue !== undefined && modelData.measuredValue !== null
                                                text: (modelData.measuredValue !== undefined && modelData.measuredValue !== null
                                                        ? Number(modelData.measuredValue).toFixed(2) : "")
                                                      + (modelData.measuredUnit && modelData.measuredUnit.length ? " " + modelData.measuredUnit : "")
                                                color: modelData.verified ? Style.good : Style.warn
                                                font.family: Style.fontFamily; font.pixelSize: Style.fsSmall; font.bold: true
                                            }
                                        }
                                    }
                                    Label {
                                        visible: srow.reportDocId > 0
                                        text: "✓ Report generated — see Documents"
                                        color: Style.good; font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                                    }
                                    Label {
                                        visible: srow.stepCount === 0
                                        text: "No steps recorded yet."
                                        color: Style.textFaint; font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                anchors.bottomMargin: srow.open ? scol.implicitHeight : 0  // taps on header toggle
                                z: -1
                                onClicked: {
                                    srow.open = !srow.open
                                    if (srow.open) srow.stepRows = labModel.steps(srow.sessionId)
                                }
                            }
                            // Keep an open session's steps fresh as the agent advances it.
                            Connections {
                                target: labModel
                                function onLiveStepChanged() {
                                    if (srow.open && labModel.liveSessionId === srow.sessionId)
                                        srow.stepRows = labModel.steps(srow.sessionId)
                                }
                            }
                        }

                        EmptyState {
                            anchors.fill: parent
                            visible: sessList.count === 0
                            glyph: "⌕"
                            title: "No lab sessions yet"
                            hint: "Say \"start a lab session for …\" (or use the Lab Guide personality). Hearth will guide each step and verify your measurements."
                        }
                    }
                }
            }
        }
    }
}
