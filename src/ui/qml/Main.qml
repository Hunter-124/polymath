import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// Main shell — frameless holographic chrome (01 §4).
// Feature wiring (palette registry, ToastStack, bell/center, SurfaceHost,
// Style↔settings Bindings) lands in C1 — see // C1 HOOK markers below.
ApplicationWindow {
    id: window
    width: 1280
    height: 820
    visible: true
    title: "Polymath — Local AI Home Assistant"
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint

    // Bundle Inter (SIL OFL) app-wide.
    FontLoader {
        id: inter
        source: "qrc:/qt/qml/Polymath/fonts/Inter.ttf"
        onStatusChanged: if (status === FontLoader.Ready) Style.fontFamily = inter.font.family
    }
    Component.onCompleted: {
        if (inter.status === FontLoader.Ready)
            Style.fontFamily = inter.font.family
        // Capture path sets pmEffectsEnabled=false; live app true. C1 adds full settings bridge.
        if (typeof pmEffectsEnabled !== "undefined")
            Style.effectsEnabled = pmEffectsEnabled
    }
    font.family: Style.fontFamily

    // --- page registry (icon + group for grouped rail) ----------------------
    readonly property var pages: [
        { name: "Dashboard",     src: "Dashboard.qml",          icon: "home",     group: "CORE" },
        { name: "Chat",          src: "ChatView.qml",           icon: "chat",     group: "CORE" },
        { name: "Cameras",       src: "CamerasView.qml",        icon: "camera",   group: "SENSE" },
        { name: "Timeline",      src: "TimelineView.qml",       icon: "clock",    group: "SENSE" },
        { name: "Tasks",         src: "TaskQueueView.qml",      icon: "tasks",    group: "WORK" },
        { name: "Shopping",      src: "ShoppingView.qml",       icon: "cart",     group: "WORK" },
        { name: "Agents",        src: "AgentSessionsView.qml",  icon: "flask",    group: "WORK" },
        { name: "Personalities", src: "PersonalitiesView.qml",  icon: "person",   group: "SYSTEM" },
        { name: "Models",        src: "ModelManagerView.qml",   icon: "chip",     group: "SYSTEM" },
        { name: "Privacy",       src: "PrivacyView.qml",        icon: "shield",   group: "SYSTEM" },
        { name: "Mobile Access", src: "MobileAccessView.qml",   icon: "phone",    group: "SYSTEM" },
        { name: "Settings",      src: "SettingsView.qml",       icon: "settings", group: "SYSTEM" }
    ]
    readonly property var navGroups: ["CORE", "SENSE", "WORK", "SYSTEM"]

    property bool railCollapsed: false
    property int currentPage: 0
    readonly property bool isMaximized: window.visibility === Window.Maximized

    function pageIndexOf(name) {
        for (var i = 0; i < pages.length; ++i)
            if (pages[i].name === name) return i
        return -1
    }
    function goToPage(name) {
        var i = pageIndexOf(name)
        if (i >= 0) currentPage = i
    }
    // C1 HOOK: openSettings(section) deep-link for palette / surface host
    function openSettings(section) {
        goToPage("Settings")
        // C1: set focusSection on Settings loader item when wired
    }
    function toggleMaximize() {
        if (isMaximized) window.showNormal()
        else window.showMaximized()
    }

    // =====================================================================
    // Z0 — aurora wallpaper
    // =====================================================================
    AuroraBackground {
        id: aurora
        anchors.fill: parent
        z: 0
    }

    // =====================================================================
    // Z1 — nav rail + page host
    // =====================================================================
    Item {
        id: shellBody
        anchors.fill: parent
        anchors.topMargin: titlebar.height + (window.isMaximized ? Style.gapSm : 0)
        anchors.leftMargin: window.isMaximized ? Style.gapSm : 0
        anchors.rightMargin: window.isMaximized ? Style.gapSm : 0
        anchors.bottomMargin: window.isMaximized ? Style.gapSm : 0
        z: 1

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // --- Navigation rail ------------------------------------------
            GlassSurface {
                id: navRail
                Layout.fillHeight: true
                Layout.preferredWidth: window.railCollapsed ? Style.railWidthCollapsed : Style.railWidth
                radius: 0
                elevation: 1
                section: ""
                Behavior on Layout.preferredWidth {
                    NumberAnimation { duration: Style.durSlow; easing.type: Easing.OutCubic }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Style.gapSm
                    spacing: Style.gapSm

                    // Collapse toggle
                    PmToolButton {
                        Layout.alignment: window.railCollapsed ? Qt.AlignHCenter : Qt.AlignLeft
                        iconName: window.railCollapsed ? "chevron-right" : "chevron-left"
                        onClicked: window.railCollapsed = !window.railCollapsed
                        PmTooltip {
                            text: window.railCollapsed ? "Expand nav" : "Collapse nav"
                            visible: window.railCollapsed
                        }
                    }

                    // Listening / persona status card
                    GlassCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: statusInner.implicitHeight + Style.gap
                        section: "Dashboard"
                        visible: !window.railCollapsed || true
                        ColumnLayout {
                            id: statusInner
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: Style.gapSm
                            spacing: 4
                            RowLayout {
                                spacing: Style.gapSm
                                Layout.alignment: Qt.AlignHCenter
                                PmStatusDot {
                                    tone: app.listening ? Style.good : Style.textFaint
                                    pulsing: app.listening
                                    Layout.alignment: Qt.AlignVCenter
                                }
                                ColumnLayout {
                                    spacing: 1
                                    visible: !window.railCollapsed
                                    Layout.fillWidth: true
                                    Text {
                                        text: app.listening ? "Listening" : "Idle"
                                        color: app.listening ? Style.good : Style.textDim
                                        font.family: Style.fontFamily
                                        font.pixelSize: Style.fsSmall
                                        font.bold: true
                                    }
                                    Text {
                                        text: app.activePersonality
                                        color: Style.textFaint
                                        font.family: Style.fontFamily
                                        font.pixelSize: Style.fsTiny
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                            Text {
                                visible: !window.railCollapsed
                                text: app.modelStatus
                                color: Style.textFaint
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }

                    // Grouped nav entries
                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentHeight: navCol.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: PmScrollBar { tone: Style.accent }

                        ColumnLayout {
                            id: navCol
                            width: parent.width
                            spacing: 2

                            Repeater {
                                model: window.navGroups
                                delegate: ColumnLayout {
                                    id: groupBlock
                                    required property string modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    spacing: 2

                                    // Group caption (hidden when collapsed)
                                    Text {
                                        visible: !window.railCollapsed
                                        text: groupBlock.modelData
                                        color: Style.textFaint
                                        font.family: Style.fontFamily
                                        font.pixelSize: Style.fsTiny
                                        font.letterSpacing: Style.letterSpaceWide
                                        font.bold: true
                                        Layout.topMargin: groupBlock.index === 0 ? 0 : Style.gapSm
                                        Layout.leftMargin: Style.gapSm
                                        Layout.bottomMargin: 2
                                    }

                                    Repeater {
                                        model: {
                                            var out = []
                                            for (var i = 0; i < window.pages.length; ++i) {
                                                if (window.pages[i].group === groupBlock.modelData)
                                                    out.push({ page: window.pages[i], pageIndex: i })
                                            }
                                            return out
                                        }
                                        delegate: PmButton {
                                            id: navBtn
                                            required property var modelData
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: Style.controlH
                                            flat: true
                                            tone: Style.sectionColor(modelData.page.name)
                                            highlighted: window.currentPage === modelData.pageIndex
                                            onClicked: window.currentPage = modelData.pageIndex

                                            contentItem: Item {
                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 4
                                                    anchors.rightMargin: 4
                                                    spacing: Style.gapSm

                                                    // Active section bar
                                                    Rectangle {
                                                        width: 3
                                                        height: 16
                                                        radius: 1.5
                                                        color: navBtn.highlighted
                                                               ? Style.sectionColor(navBtn.modelData.page.name)
                                                               : "transparent"
                                                        Layout.alignment: Qt.AlignVCenter
                                                        // Soft glow when active
                                                        Rectangle {
                                                            anchors.centerIn: parent
                                                            width: 8; height: 22; radius: 4
                                                            z: -1
                                                            visible: navBtn.highlighted
                                                            color: Style.sectionGlow(navBtn.modelData.page.name, 0.35)
                                                        }
                                                    }
                                                    PmIcon {
                                                        Layout.preferredWidth: Style.iconSm
                                                        Layout.preferredHeight: Style.iconSm
                                                        Layout.alignment: Qt.AlignVCenter
                                                        name: navBtn.modelData.page.icon
                                                        color: navBtn.highlighted
                                                               ? Style.sectionColor(navBtn.modelData.page.name)
                                                               : Style.textDim
                                                    }
                                                    Text {
                                                        visible: !window.railCollapsed
                                                        opacity: window.railCollapsed ? 0 : 1
                                                        text: navBtn.modelData.page.name
                                                        font.family: Style.fontFamily
                                                        font.pixelSize: Style.fsBody
                                                        color: navBtn.highlighted
                                                               ? Style.sectionColor(navBtn.modelData.page.name)
                                                               : Style.text
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                        verticalAlignment: Text.AlignVCenter
                                                        Behavior on opacity {
                                                            NumberAnimation { duration: Style.durFast }
                                                        }
                                                    }
                                                }
                                            }

                                            PmTooltip {
                                                text: navBtn.modelData.page.name
                                                // only useful when collapsed
                                                delay: 300
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Push-to-talk — press-and-hold semantics preserved
                    PmButton {
                        id: ptt
                        Layout.fillWidth: true
                        Layout.preferredHeight: Style.controlH
                        accent: down
                        tone: Style.accent
                        text: down
                              ? (window.railCollapsed ? "" : "Listening…")
                              : (window.railCollapsed ? "" : "Push to talk")
                        onPressed: app.pushToTalk(true)
                        onReleased: app.pushToTalk(false)

                        contentItem: RowLayout {
                            spacing: Style.gapSm
                            anchors.centerIn: parent
                            PmIcon {
                                Layout.preferredWidth: Style.iconSm
                                Layout.preferredHeight: Style.iconSm
                                name: "mic"
                                color: ptt.down ? Style.accentText : Style.text
                            }
                            Text {
                                visible: !window.railCollapsed
                                text: ptt.down ? "Listening…" : "Push to talk"
                                color: ptt.down ? Style.accentText : Style.text
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsBody
                            }
                        }

                        // Pulsing glow while held
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -3
                            radius: Style.radiusSm + 3
                            z: -1
                            visible: ptt.down
                            color: "transparent"
                            border.width: 2
                            border.color: Style.tint(Style.accent, 0.45)
                            SequentialAnimation on opacity {
                                running: ptt.down
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.35; duration: 600; easing.type: Easing.InOutSine }
                                NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
                            }
                        }
                    }
                }
            }

            // --- PageHost (always-active Loaders + transitions) -----------
            Item {
                id: pageHost
                Layout.fillWidth: true
                Layout.fillHeight: true

                Repeater {
                    model: window.pages
                    delegate: Loader {
                        id: pageLoader
                        required property var modelData
                        required property int index
                        anchors.fill: parent
                        source: modelData.src
                        active: true
                        // Keep non-current pages invisible but alive (timers / onCompleted).
                        opacity: index === window.currentPage ? 1 : 0
                        enabled: index === window.currentPage
                        z: index === window.currentPage ? 1 : 0
                        y: {
                            if (Style.reduceMotion)
                                return 0
                            return index === window.currentPage ? 0 : Style.gap
                        }
                        Behavior on opacity {
                            NumberAnimation {
                                duration: Style.reduceMotion ? Style.durFast : Style.durBase
                                easing.type: Easing.OutCubic
                            }
                        }
                        Behavior on y {
                            enabled: !Style.reduceMotion
                            NumberAnimation {
                                duration: Style.durBase
                                easing.type: Easing.OutCubic
                            }
                        }
                    }
                }
            }
        }
    }

    // =====================================================================
    // Z2 — titlebar
    // =====================================================================
    GlassSurface {
        id: titlebar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 40
        z: 2
        radius: 0
        elevation: 1

        // Drag to move; double-click toggles maximize
        DragHandler {
            target: null
            onActiveChanged: if (active) window.startSystemMove()
        }
        TapHandler {
            onDoubleTapped: window.toggleMaximize()
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Style.gap
            anchors.rightMargin: Style.gapSm
            spacing: Style.gapSm

            // Wordmark
            Text {
                text: "POLYMATH"
                color: Style.accent
                font.family: Style.fontFamily
                font.pixelSize: Style.fsBody
                font.bold: true
                font.letterSpacing: Style.letterSpaceWide
                Layout.alignment: Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            // Command-palette pill (C1 wires open)
            Rectangle {
                id: palettePill
                Layout.preferredWidth: 240
                Layout.preferredHeight: Style.controlHsm
                Layout.alignment: Qt.AlignVCenter
                radius: Style.radiusPill
                color: Qt.rgba(1, 1, 1, 0.04)
                border.width: 1
                border.color: Style.glassBorder
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Style.gapSm
                    anchors.rightMargin: Style.gapSm
                    spacing: 6
                    PmIcon {
                        Layout.preferredWidth: Style.iconSm
                        Layout.preferredHeight: Style.iconSm
                        name: "search"
                        color: Style.textFaint
                    }
                    Text {
                        text: "Search · ask anything"
                        color: Style.textFaint
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsSmall
                        Layout.fillWidth: true
                    }
                    Text {
                        text: "Ctrl+K"
                        color: Style.textFaint
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsTiny
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    // C1 HOOK: palette.open() / openPalette()
                    onClicked: {
                        // C1: commandPalette.openPalette()
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Listening indicator
            PmStatusDot {
                Layout.alignment: Qt.AlignVCenter
                tone: app.listening ? Style.good : Style.textFaint
                pulsing: app.listening
            }

            // C1 HOOK: NotificationBell + NotificationCenter
            // NotificationBell {
            //     id: notifBell
            //     unreadCount: typeof notifications !== "undefined" ? notifications.unreadCount : 0
            //     onClicked: notifCenter.open = !notifCenter.open
            // }

            // Window chrome
            Row {
                spacing: 2
                Layout.alignment: Qt.AlignVCenter

                component ChromeBtn: Item {
                    id: cbtn
                    property string glyph: ""
                    property bool isClose: false
                    signal activated()
                    width: 36; height: 28
                    Rectangle {
                        anchors.fill: parent
                        radius: Style.radiusXs
                        color: cbtnMA.containsMouse
                               ? (cbtn.isClose ? Style.tint(Style.bad, 0.85) : Qt.rgba(1, 1, 1, 0.06))
                               : "transparent"
                    }
                    Text {
                        anchors.centerIn: parent
                        text: cbtn.glyph
                        color: cbtnMA.containsMouse && cbtn.isClose ? Style.text : Style.textDim
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsBody
                    }
                    MouseArea {
                        id: cbtnMA
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: cbtn.activated()
                    }
                }

                ChromeBtn {
                    glyph: "-"
                    onActivated: window.showMinimized()
                }
                ChromeBtn {
                    glyph: window.isMaximized ? "s" : "O"
                    onActivated: window.toggleMaximize()
                }
                ChromeBtn {
                    glyph: "x"
                    isClose: true
                    onActivated: window.close()
                }
            }
        }
    }

    // =====================================================================
    // Z3 — overlays (C1 wires real components; interim toast kept for notices)
    // =====================================================================

    // C1 HOOK: ToastStack { id: toastStack; anchors.fill: parent; z: 3 }
    // C1 HOOK: CommandPalette { id: commandPalette; actions: paletteActions; z: 3 }
    // C1 HOOK: SurfaceHost { id: surfaceHost; anchors.fill: parent; z: 3 }
    // C1 HOOK: Style↔settings Bindings (accent, fontScale, effects, reduceMotion)
    // C1 HOOK: Shortcut { sequence: "Ctrl+K"; onActivated: commandPalette.openPalette() }
    // C1 HOOK: paletteActions registry + registerAction / unregisterAction

    // Interim toast (pre-C1) — preserves app.onNoticePosted contract.
    Connections {
        target: app
        function onNoticePosted(level, source, message) {
            toast.accentColor = level === "error" ? Style.bad
                               : level === "warn" ? Style.warn : Style.accent
            toast.text = source + ": " + message
            toast.visible = true
            toastTimer.restart()
        }
    }
    Rectangle {
        id: toast
        property alias text: toastLabel.text
        property color accentColor: Style.accent
        visible: false
        z: 3
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            bottomMargin: Style.pad
        }
        width: toastRow.implicitWidth + Style.pad
        height: Style.controlH + 6
        radius: Style.radiusSm
        color: Style.surface3
        border.width: 1
        border.color: Style.border
        RowLayout {
            id: toastRow
            anchors.centerIn: parent
            spacing: Style.gapSm
            Rectangle {
                width: 6; height: 20; radius: 3
                color: toast.accentColor
            }
            Label {
                id: toastLabel
                color: Style.text
                font.family: Style.fontFamily
                font.pixelSize: Style.fsBody
            }
        }
        Timer {
            id: toastTimer
            interval: 4000
            onTriggered: toast.visible = false
        }
    }

    // --- Resize handles (8 px edges/corners) --------------------------------
    component ResizeEdge: MouseArea {
        property int edges: 0
        hoverEnabled: true
        onPressed: window.startSystemResize(edges)
    }

    // Edges
    ResizeEdge {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 8; edges: Qt.LeftEdge; cursorShape: Qt.SizeHorCursor; z: 4
    }
    ResizeEdge {
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        width: 8; edges: Qt.RightEdge; cursorShape: Qt.SizeHorCursor; z: 4
    }
    ResizeEdge {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 8; edges: Qt.TopEdge; cursorShape: Qt.SizeVerCursor; z: 4
    }
    ResizeEdge {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 8; edges: Qt.BottomEdge; cursorShape: Qt.SizeVerCursor; z: 4
    }
    // Corners
    ResizeEdge {
        anchors { left: parent.left; top: parent.top }
        width: 12; height: 12; edges: Qt.LeftEdge | Qt.TopEdge
        cursorShape: Qt.SizeFDiagCursor; z: 5
    }
    ResizeEdge {
        anchors { right: parent.right; top: parent.top }
        width: 12; height: 12; edges: Qt.RightEdge | Qt.TopEdge
        cursorShape: Qt.SizeBDiagCursor; z: 5
    }
    ResizeEdge {
        anchors { left: parent.left; bottom: parent.bottom }
        width: 12; height: 12; edges: Qt.LeftEdge | Qt.BottomEdge
        cursorShape: Qt.SizeBDiagCursor; z: 5
    }
    ResizeEdge {
        anchors { right: parent.right; bottom: parent.bottom }
        width: 12; height: 12; edges: Qt.RightEdge | Qt.BottomEdge
        cursorShape: Qt.SizeFDiagCursor; z: 5
    }
}
