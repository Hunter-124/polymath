import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Dashboard — cyan section + HUD (01 §5.1, 02 §F4).
Item {
    id: root
    // app.hasModels is a live property on AppController (true once a usable model
    // is registered on disk); the cold-start banner shows while it is false.
    readonly property bool hasModels: app.hasModels

    // Defensive HUD bindings (stubs may omit some props until full wire).
    readonly property int vramUsed: (typeof app.vramUsedMiB !== "undefined") ? app.vramUsedMiB : 0
    readonly property int vramTotal: (typeof app.vramTotalMiB !== "undefined") ? app.vramTotalMiB : 0
    readonly property real vramRatio: vramTotal > 0 ? (vramUsed / vramTotal) : 0
    readonly property bool vramWarn: vramRatio > 0.85

    function taskCount(status) {
        if (typeof taskModel === "undefined" || !taskModel)
            return 0
        // Prefer role-aware count if model exposes helper; else scan rows defensively.
        if (typeof taskModel.countByStatus === "function")
            return taskModel.countByStatus(status)
        if (status === "running" && taskModel.runningCount !== undefined)
            return taskModel.runningCount
        if (status === "queued" && taskModel.queuedCount !== undefined)
            return taskModel.queuedCount
        return 0
    }

    readonly property int runningTasks: root.taskCount("running")
    readonly property int queuedTasks: root.taskCount("queued")

    property var modelRows: []
    function reloadModels() {
        try { modelRows = app.models() || [] } catch (e) { modelRows = [] }
    }
    Component.onCompleted: reloadModels()
    Connections {
        target: app
        ignoreUnknownSignals: true
        function onModelsChanged() { root.reloadModels() }
        function onWakeWordPulse() { wakePing.restart() }
    }

    // Resident (active) model chips from app.models()
    function residentChips() {
        var out = []
        for (var i = 0; i < modelRows.length; ++i) {
            var m = modelRows[i]
            if (m && m.active === true)
                out.push(m)
        }
        return out
    }

    // Agent sessions summary — bind only when model exists (05 lands later).
    readonly property int agentSessionCount: {
        if (typeof agentSessions === "undefined" || !agentSessions)
            return -1
        if (agentSessions.count !== undefined)
            return agentSessions.count
        if (typeof agentSessions.rowCount === "function")
            return agentSessions.rowCount()
        return -1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gapLg

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Dashboard"
            section: "Dashboard"
            subtitle: "At-a-glance status, VRAM, and shortcuts"
        }

        // Cold-start banner: no Fast model -> guide to fetch-models.
        GlassCard {
            visible: !root.hasModels
            Layout.fillWidth: true
            Layout.preferredHeight: coldCol.implicitHeight + Style.padSm
            section: "Dashboard"
            tintColor: Style.warn
            tintAlpha: 0.12

            RowLayout {
                anchors.fill: parent
                anchors.margins: Style.padSm
                spacing: Style.gap
                Rectangle {
                    width: 28; height: 28; radius: 14
                    color: Style.tint(Style.warn, 0.18)
                    border.width: 1
                    border.color: Style.warn
                    Layout.alignment: Qt.AlignVCenter
                    Text {
                        anchors.centerIn: parent
                        text: "!"
                        color: Style.warn
                        font.bold: true
                        font.pixelSize: Style.fsHeading
                    }
                }
                ColumnLayout {
                    id: coldCol
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: "No model loaded — finish setup"
                        color: Style.warn
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsBody
                        font.bold: true
                    }
                    Text {
                        text: "Run  scripts/fetch-models.ps1  to download the default local models, then open Models to confirm roles."
                        color: Style.textDim
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // Stat cards row
        GridLayout {
            columns: 3
            columnSpacing: Style.gapLg
            rowSpacing: Style.gapLg
            Layout.fillWidth: true

            GlassCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 124
                section: "Dashboard"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Style.padSm
                    spacing: 4
                    RowLayout {
                        spacing: Style.gapSm
                        PmIcon {
                            name: "mic"
                            color: Style.sectionColor("Dashboard")
                            Layout.preferredWidth: Style.iconMd
                            Layout.preferredHeight: Style.iconMd
                        }
                        Text {
                            text: "Assistant"
                            color: Style.sectionColor("Dashboard")
                            font.family: Style.fontFamily
                            font.bold: true
                            font.pixelSize: Style.fsBody
                        }
                        Item { Layout.fillWidth: true }
                        // Wake-word ping flash
                        Rectangle {
                            id: wakeFlash
                            width: 10; height: 10; radius: 5
                            color: Style.accent
                            opacity: 0
                            SequentialAnimation {
                                id: wakePing
                                NumberAnimation { target: wakeFlash; property: "opacity"; to: 1; duration: 80 }
                                NumberAnimation { target: wakeFlash; property: "opacity"; to: 0; duration: 700; easing.type: Easing.OutCubic }
                            }
                        }
                    }
                    RowLayout {
                        spacing: Style.gapSm
                        PmStatusDot {
                            tone: app.listening ? Style.good : Style.textFaint
                            pulsing: app.listening
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Text {
                            text: app.listening ? "Listening" : "Idle"
                            color: Style.text
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsHeading
                        }
                    }
                    Text {
                        text: app.activePersonality
                        color: Style.textFaint
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsSmall
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            GlassCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 124
                section: "Models"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Style.padSm
                    spacing: 4
                    RowLayout {
                        spacing: Style.gapSm
                        PmIcon {
                            name: "chip"
                            color: Style.sectionColor("Models")
                            Layout.preferredWidth: Style.iconMd
                            Layout.preferredHeight: Style.iconMd
                        }
                        Text {
                            text: "Model"
                            color: Style.sectionColor("Models")
                            font.family: Style.fontFamily
                            font.bold: true
                            font.pixelSize: Style.fsBody
                        }
                    }
                    Text {
                        text: app.modelStatus
                        color: root.hasModels ? Style.text : Style.warn
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsBody
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    // Resident chips
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: root.residentChips()
                            delegate: PmBadge {
                                required property var modelData
                                text: modelData.displayName || modelData.role || "resident"
                                tone: Style.good
                                filled: true
                            }
                        }
                    }
                }
            }

            GlassCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 124
                section: "Tasks"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Style.padSm
                    spacing: 4
                    RowLayout {
                        spacing: Style.gapSm
                        PmIcon {
                            name: "tasks"
                            color: Style.sectionColor("Tasks")
                            Layout.preferredWidth: Style.iconMd
                            Layout.preferredHeight: Style.iconMd
                        }
                        Text {
                            text: "Tasks"
                            color: Style.sectionColor("Tasks")
                            font.family: Style.fontFamily
                            font.bold: true
                            font.pixelSize: Style.fsBody
                        }
                    }
                    RowLayout {
                        spacing: Style.gap
                        ColumnLayout {
                            spacing: 0
                            Text {
                                text: String(root.runningTasks)
                                color: Style.text
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsHeading
                                font.bold: true
                            }
                            Text {
                                text: "running"
                                color: Style.textFaint
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny
                            }
                        }
                        ColumnLayout {
                            spacing: 0
                            Text {
                                text: String(root.queuedTasks)
                                color: Style.text
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsHeading
                                font.bold: true
                            }
                            Text {
                                text: "queued"
                                color: Style.textFaint
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny
                            }
                        }
                        Item { Layout.fillWidth: true }
                        PmStatusDot {
                            visible: root.runningTasks > 0
                            tone: Style.accent
                            pulsing: true
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                    Text {
                        visible: root.agentSessionCount >= 0
                        text: root.agentSessionCount + " agent session"
                              + (root.agentSessionCount === 1 ? "" : "s")
                        color: Style.textFaint
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsTiny
                    }
                }
            }
        }

        // VRAM gauge
        GlassCard {
            Layout.fillWidth: true
            Layout.preferredHeight: vramCol.implicitHeight + Style.padSm * 2
            section: "Models"
            visible: root.vramTotal > 0

            ColumnLayout {
                id: vramCol
                anchors.fill: parent
                anchors.margins: Style.padSm
                spacing: Style.gapSm
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "VRAM"
                        color: Style.sectionColor("Models")
                        font.family: Style.fontFamily
                        font.bold: true
                        font.pixelSize: Style.fsBody
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: root.vramUsed + " / " + root.vramTotal + " MiB"
                        color: root.vramWarn ? Style.warn : Style.textDim
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsSmall
                    }
                    PmBadge {
                        visible: root.vramWarn
                        text: "high"
                        tone: Style.warn
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 8
                    radius: 4
                    color: Qt.rgba(1, 1, 1, 0.06)
                    Rectangle {
                        width: parent.width * Math.min(1, root.vramRatio)
                        height: parent.height
                        radius: parent.radius
                        color: root.vramWarn ? Style.warn : Style.accent
                        Behavior on width {
                            NumberAnimation { duration: Style.durBase; easing.type: Easing.OutCubic }
                        }
                    }
                }
            }
        }

        // Ask anything empty surface
        GlassCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            section: "Dashboard"
            EmptyState {
                anchors.fill: parent
                glyph: "+"
                iconName: "sparkle"
                glyphColor: Style.sectionColor("Dashboard")
                title: "Ask Polymath anything"
                hint: "Open Chat or hold Push-to-talk. Try \"where did I leave my keys?\", \"add milk to the shopping list\", or \"summarise today\"."
            }
        }
    }
}
