import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt.labs.platform as Platform
import Polymath

// Main shell — frameless holographic chrome (01 §4).
// C1: palette registry + Ctrl+K, ToastStack, bell/center, SurfaceHost,
// Style↔settings Bindings (02 execution-order step 3).
ApplicationWindow {
    id: window
    width: 1280
    height: 820
    visible: true
    title: "Polymath — Local AI Home Assistant"
    color: "transparent"
    // Keep minimize/system-menu hints so Windows still gives us a stable
    // taskbar button + restore path even though the chrome is frameless.
    flags: Qt.Window | Qt.FramelessWindowHint
           | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint
           | Qt.WindowSystemMenuHint

    // Bundle Inter (SIL OFL) app-wide. Prefer the stable "Inter" family name —
    // variable-font family strings ("Inter Variable") trip DirectWrite on some
    // Windows builds and flood the log with CreateFontFaceFromHDC failures.
    FontLoader {
        id: inter
        source: "qrc:/qt/qml/Polymath/fonts/Inter.ttf"
        onStatusChanged: {
            if (status !== FontLoader.Ready) return
            var fam = inter.font.family
            Style.fontFamily = (fam && fam.indexOf("Inter") === 0) ? "Inter" : (fam || "Segoe UI")
        }
    }
    Component.onCompleted: {
        if (inter.status === FontLoader.Ready) {
            var fam = inter.font.family
            Style.fontFamily = (fam && fam.indexOf("Inter") === 0) ? "Inter" : (fam || "Segoe UI")
        } else if (!Style.fontFamily) {
            Style.fontFamily = "Segoe UI"
        }
        // Ensure we land on a real monitor (multi-display / off-screen restore).
        if (x < -width + 80 || y < -40) {
            x = 80; y = 60
        }
    }
    font.family: Style.fontFamily || "Segoe UI"

    // --- restore helpers (taskbar / tray) ------------------------------------
    function restoreWindow() {
        if (!window.visible)
            window.show()
        if (window.visibility === Window.Minimized)
            window.showNormal()
        window.raise()
        window.requestActivate()
    }

    // Close / Alt+F4 hide to tray instead of killing the process. Real quit is
    // via the tray menu (or when the tray is unavailable).
    onClosing: function (close) {
        if (tray.available) {
            close.accepted = false
            window.hide()
        }
    }

    Platform.SystemTrayIcon {
        id: tray
        visible: true
        tooltip: "Polymath — Local AI Home Assistant"
        icon.source: "qrc:/qt/qml/Polymath/icons/tray.png"
        onActivated: function (reason) {
            // Trigger = single left click; DoubleClick also restores.
            if (reason === Platform.SystemTrayIcon.Trigger
                    || reason === Platform.SystemTrayIcon.DoubleClick
                    || reason === Platform.SystemTrayIcon.MiddleClick) {
                window.restoreWindow()
            }
        }
        menu: Platform.Menu {
            Platform.MenuItem {
                text: qsTr("Show Polymath")
                onTriggered: window.restoreWindow()
            }
            Platform.MenuItem {
                text: qsTr("Quit")
                onTriggered: Qt.quit()
            }
        }
    }

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

    // --- E4: canonical page-id → nav rail display-name map -------------------
    // Contract fixed by A3 (docs/overhaul2/results/A3_notes.md) — ui_control
    // open_page publishes NavigateRequest(page) with one of these ids.
    readonly property var pageIdMap: ({
        "dashboard":     "Dashboard",
        "chat":          "Chat",
        "cameras":       "Cameras",
        "timeline":      "Timeline",
        "tasks":         "Tasks",
        "shopping":      "Shopping",
        "agents":        "Agents",
        "personalities": "Personalities",
        "models":        "Models",
        "privacy":       "Privacy",
        "mobile_access": "Mobile Access",
        "settings":      "Settings"
    })

    // --- E4: AI window-takeover state (ui_control "window" verbs) -----------
    property bool aiTakeoverActive: false   // AI currently holds fullscreen/on-top/present
    property bool aiOnTop: false            // AI-forced always-on-top currently applied
    property bool priorWasMaximized: false  // maximize state to restore when exiting fullscreen
    property int  presentTimeoutMin: 30     // ui.present_timeout_min, read defensively (default 30)

    // AI / future-registered palette actions (02 §F2)
    property var dynamicActions: []

    // Combined action list for CommandPalette (rebinds when dynamicActions changes)
    readonly property var paletteActions: {
        var dyn = dynamicActions
        var acts = []
        var i
        for (i = 0; i < pages.length; ++i) {
            var p = pages[i]
            acts.push({
                id: "nav." + String(p.name).toLowerCase().replace(/ /g, "_"),
                title: "Go to " + p.name,
                section: "Navigate",
                run: (function (name) {
                    return function () { window.goToPage(name) }
                })(p.name)
            })
        }
        acts.push({
            id: "ptt.toggle",
            title: "Toggle push-to-talk",
            section: "Voice",
            run: function () {
                if (typeof app !== "undefined")
                    app.pushToTalk(!app.listening)
            }
        })
        acts.push({
            id: "chat.focus",
            title: "Focus chat input",
            section: "Chat",
            run: function () {
                window.goToPage("Chat")
                Qt.callLater(function () {
                    var item = window.pageLoaderItem("Chat")
                    if (item && typeof item.focusInput === "function")
                        item.focusInput()
                    else if (item)
                        item.forceActiveFocus()
                })
            }
        })
        acts.push({
            id: "shop.add",
            title: "Add shopping item…",
            section: "Create",
            run: function () { window.goToPage("Shopping") }
        })
        acts.push({
            id: "settings.appearance",
            title: "Settings: Appearance",
            section: "Settings",
            run: function () { window.openSettings("appearance") }
        })
        acts.push({
            id: "settings.audio",
            title: "Settings: Audio",
            section: "Settings",
            run: function () { window.openSettings("audio") }
        })
        acts.push({
            id: "settings.agents",
            title: "Settings: Agents",
            section: "Settings",
            run: function () { window.openSettings("agents") }
        })
        acts.push({
            id: "settings.safety",
            title: "Settings: Safety",
            section: "Settings",
            run: function () { window.openSettings("safety") }
        })
        acts.push({
            id: "ui.effects.toggle",
            title: "Toggle glass effects",
            section: "Appearance",
            run: function () {
                if (typeof settings !== "undefined")
                    settings.effects = !settings.effects
            }
        })
        acts.push({
            id: "surface.demo",
            title: "Spawn placeholder surface",
            section: "Dev",
            run: function () {
                if (typeof app !== "undefined")
                    app.spawnSurfaceDemo()
            }
        })
        acts.push({
            id: "agent.spawn",
            title: "New agent session…",
            section: "Agents",
            run: function () { window.goToPage("Agents") }
        })
        acts.push({
            id: "app.quit",
            title: "Quit Polymath",
            section: "System",
            run: function () { Qt.quit() }
        })
        return acts.concat(dyn)
    }

    function pageIndexOf(name) {
        for (var i = 0; i < pages.length; ++i)
            if (pages[i].name === name) return i
        return -1
    }
    function goToPage(name) {
        var i = pageIndexOf(name)
        if (i >= 0) currentPage = i
    }
    // E4: navigate by canonical page id (from ui_control open_page).
    function goToPageId(id) {
        var name = window.pageIdMap[String(id || "").toLowerCase()]
        if (name) window.goToPage(name)
    }
    // Resolve always-active PageHost Loader item by page name
    function pageLoaderItem(name) {
        var idx = pageIndexOf(name)
        if (idx < 0) return null
        var kids = pageHost.children
        for (var i = 0; i < kids.length; ++i) {
            var c = kids[i]
            if (c && c.index === idx)
                return c.item
        }
        return null
    }
    // Deep-link Settings to a section (SettingsView.focusSection)
    function openSettings(section) {
        goToPage("Settings")
        Qt.callLater(function () {
            var item = window.pageLoaderItem("Settings")
            if (item && section)
                item.focusSection = section
        })
    }
    function registerAction(a) {
        if (!a || !a.id) return
        var next = dynamicActions.slice()
        for (var i = 0; i < next.length; ++i) {
            if (next[i].id === a.id) {
                next[i] = a
                dynamicActions = next
                return
            }
        }
        next.push(a)
        dynamicActions = next
    }
    function unregisterAction(id) {
        dynamicActions = dynamicActions.filter(function (a) { return a.id !== id })
    }
    function toggleMaximize() {
        if (isMaximized) window.showNormal()
        else window.showMaximized()
    }

    // =====================================================================
    // E4 — AI window takeover (ui_control "window" verbs, A3 relay)
    // =====================================================================

    // Begin/refresh an AI-held takeover state: remembers the pre-takeover
    // maximize state (once, so repeated verbs don't clobber it) and
    // (re)starts the human-override auto-revert timer. `ui.present_timeout_min`
    // is owned by A4/config this batch — read it defensively with a 30-minute
    // fallback so this node never depends on that key existing.
    function aiBeginTakeover() {
        if (!window.aiTakeoverActive)
            window.priorWasMaximized = window.isMaximized
        window.aiTakeoverActive = true
        var mins = 30
        if (typeof settings !== "undefined" && settings
                && typeof settings.getInt === "function") {
            var v = settings.getInt("ui.present_timeout_min", 30)
            if (v > 0) mins = v
        }
        window.presentTimeoutMin = mins
        presentTimeoutTimer.interval = mins * 60000
        presentTimeoutTimer.restart()
    }

    // Human escape hatch — Esc key, the pill's own click target, or the
    // timeout all funnel here. Restores whatever maximize state preceded
    // the takeover and drops always-on-top cleanly (Qt needs the
    // hide/show juggle for a flags change to actually take effect).
    function aiExitTakeover() {
        presentTimeoutTimer.stop()
        if (window.aiOnTop) {
            window.aiOnTop = false
            window.flags = window.flags & ~Qt.WindowStaysOnTopHint
            window.hide()
            window.show()
        }
        if (window.visibility === Window.FullScreen) {
            if (window.priorWasMaximized) window.showMaximized()
            else window.showNormal()
        }
        window.aiTakeoverActive = false
        window.raise()
        window.requestActivate()
    }

    // Dispatch for AppController::windowRequested(verb) — fixed enum from
    // A3: present|fullscreen|restore|always_on_top|normal|raise|hide_to_tray.
    function handleWindowVerb(verb) {
        switch (verb) {
        case "present":
            // "AI takes over the screen to show you things": raise, focus,
            // and go fullscreen — the SurfaceHost overlay (z:3, already
            // always-on-top of the page stack) is shown by construction.
            window.aiBeginTakeover()
            window.raise()
            window.requestActivate()
            if (window.visibility !== Window.FullScreen)
                window.showFullScreen()
            break
        case "fullscreen":
            window.aiBeginTakeover()
            if (window.visibility !== Window.FullScreen)
                window.showFullScreen()
            break
        case "restore":
            presentTimeoutTimer.stop()
            if (window.visibility === Window.FullScreen) {
                if (window.priorWasMaximized) window.showMaximized()
                else window.showNormal()
            }
            if (!window.aiOnTop) window.aiTakeoverActive = false
            break
        case "always_on_top":
            window.aiBeginTakeover()
            window.aiOnTop = true
            window.flags = window.flags | Qt.WindowStaysOnTopHint
            window.hide()
            window.show()
            break
        case "normal":
            window.aiOnTop = false
            window.flags = window.flags & ~Qt.WindowStaysOnTopHint
            window.hide()
            window.show()
            if (window.visibility !== Window.FullScreen)
                window.aiTakeoverActive = false
            break
        case "raise":
            window.raise()
            window.requestActivate()
            break
        case "hide_to_tray":
            // Same path onClosing uses to keep the process (and warm models)
            // alive: hide, don't quit.
            window.hide()
            break
        default:
            console.log("Main.qml: unknown window verb:", verb)
            break
        }
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

                    // Listening / persona status card.
                    // Fixed preferred heights (not bound to child.implicitHeight)
                    // so Layouts never sees a preferredHeight ↔ implicitHeight
                    // cycle ("recursive rearrange").
                    GlassCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: window.railCollapsed ? 52 : 78
                        section: "Dashboard"
                        ColumnLayout {
                            id: statusInner
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Style.gapSm
                            spacing: 4
                            RowLayout {
                                spacing: Style.gapSm
                                Layout.fillWidth: true
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
                        id: navFlick
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentHeight: navCol.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        // Never paint a ScrollBar over tab labels. Wheel/flick
                        // still scroll; the rail is short enough that chrome
                        // is pure noise and was rendering as a vertical bar
                        // through the page names.
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AlwaysOff
                        }

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
                    onClicked: commandPalette.openPalette()
                }
            }

            Item { Layout.fillWidth: true }

            // Listening indicator
            PmStatusDot {
                Layout.alignment: Qt.AlignVCenter
                tone: app.listening ? Style.good : Style.textFaint
                pulsing: app.listening
            }

            NotificationBell {
                id: notifBell
                Layout.alignment: Qt.AlignVCenter
                unreadCount: (typeof notifications !== "undefined" && notifications)
                             ? notifications.unreadCount : 0
                open: notifCenter.open
                onClicked: notifCenter.open = !notifCenter.open
            }

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
                    // Hide to tray (onClosing intercepts). Holds process alive so
                    // models stay warm; Quit is on the tray menu.
                    onActivated: window.close()
                }
            }
        }
    }

    // =====================================================================
    // Z3 — overlays (toasts, palette, surfaces, notification center)
    // =====================================================================

    // Style ↔ settings bridge (02 §F1). Capture sets pmEffectsEnabled=false.
    Binding {
        target: Style
        property: "accent"
        value: settings.accent
        when: typeof settings !== "undefined"
    }
    Binding {
        target: Style
        property: "fontScale"
        value: settings.fontScale
        when: typeof settings !== "undefined"
    }
    Binding {
        target: Style
        property: "effectsEnabled"
        value: settings.effects && (typeof pmEffectsEnabled === "undefined" ? true : pmEffectsEnabled)
        when: typeof settings !== "undefined"
    }
    Binding {
        target: Style
        property: "effectsIntensity"
        value: settings.effectsIntensity
        when: typeof settings !== "undefined"
    }
    Binding {
        target: Style
        property: "reduceMotion"
        value: settings.reduceMotion
        when: typeof settings !== "undefined"
    }

    ToastStack {
        id: toastStack
        anchors.fill: parent
        z: 3
    }

    NotificationCenter {
        id: notifCenter
        anchors.top: titlebar.bottom
        anchors.right: parent.right
        anchors.topMargin: Style.gapSm
        anchors.rightMargin: Style.gap
        z: 3
        onNavigateRequest: function (category, id) {
            notifCenter.open = false
            if (category === "task")
                window.goToPage("Tasks")
            else if (category === "reminder")
                window.goToPage("Timeline")
        }
    }

    SurfaceHost {
        id: surfaceHost
        anchors.fill: parent
        z: 3
    }

    CommandPalette {
        id: commandPalette
        anchors.fill: parent
        actions: window.paletteActions
        z: 3
    }

    // C1: SafetyPolicy confirmation dialog (Approve / Deny / Always allow).
    ConfirmDialog {
        id: confirmDialog
    }

    Shortcut {
        sequence: "Ctrl+K"
        context: Qt.ApplicationShortcut
        onActivated: commandPalette.openPalette()
    }

    // E4: human override — Esc exits any AI-held fullscreen/on-top/present
    // state. Only grabs Escape while a takeover is actually active, so it
    // doesn't compete with CommandPalette's own Escape handling otherwise.
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: window.aiTakeoverActive
        onActivated: window.aiExitTakeover()
    }

    // E4: auto-revert an AI-held takeover after ui.present_timeout_min
    // (defensive default 30 — see aiBeginTakeover()).
    Timer {
        id: presentTimeoutTimer
        interval: window.presentTimeoutMin * 60000
        repeat: false
        onTriggered: window.aiExitTakeover()
    }

    // A3 → E4: ui_control open_page / window relays (agent-driven).
    // C1: SafetyPolicy confirmRequested → ConfirmDialog.
    Connections {
        target: typeof app !== "undefined" ? app : null
        ignoreUnknownSignals: true
        function onNavigateRequested(page) {
            window.goToPageId(page)
        }
        function onWindowRequested(verb) {
            window.handleWindowVerb(verb)
        }
        function onConfirmRequested(id, tool, summary, argsPreview, reason) {
            confirmDialog.openConfirm(id, tool, summary, argsPreview, reason)
            // Surface a warn toast so the request is visible even if the dialog
            // is behind another overlay (and for capture / accessibility).
            if (typeof toastStack !== "undefined" && toastStack.pushToast)
                toastStack.pushToast("warn", "safety",
                    summary && summary.length ? summary
                        : ("Needs approval: " + (tool || "tool")))
        }
        function onConfirmSettled(id) {
            // Voice / notification / other path answered — dismiss if showing.
            if (confirmDialog.confirmId === id) {
                confirmDialog.confirmId = ""
                confirmDialog.close()
            }
        }
    }

    // E4: "Polymath is presenting — Esc to dismiss" pill, shown whenever the
    // AI holds a fullscreen/on-top/present window state. z above everything
    // (titlebar/pages/overlays) so it stays visible even in fullscreen.
    Rectangle {
        id: presentPill
        visible: window.aiTakeoverActive
        opacity: visible ? 1 : 0
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Style.gapLg
        z: 6
        radius: Style.radiusPill
        color: Style.surface3
        border.width: 1
        border.color: Style.border
        implicitWidth: pillRow.implicitWidth + Style.gapLg * 2
        implicitHeight: Style.controlH
        Behavior on opacity {
            NumberAnimation { duration: Style.durBase; easing.type: Easing.OutCubic }
        }

        RowLayout {
            id: pillRow
            anchors.centerIn: parent
            spacing: Style.gapSm
            PmStatusDot {
                tone: Style.accent
                pulsing: true
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                text: qsTr("Polymath is presenting — Esc to dismiss")
                color: Style.text
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
                Layout.alignment: Qt.AlignVCenter
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: window.aiExitTakeover()
        }

        // Soft shadow, consistent with ToastStack's card treatment.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            anchors.topMargin: 2
            z: -1
            radius: parent.radius + 2
            color: Style.shadowA1
        }
    }

    // open_page surface actions navigate the shell (SurfaceHost leaves this to C1)
    Connections {
        target: typeof app !== "undefined" ? app : null
        ignoreUnknownSignals: true
        function onSurfaceRequested(id, action, type, title, argsJson) {
            if (action !== "open_page")
                return
            var page = title || type || ""
            if ((!page || page.length === 0) && argsJson && argsJson.length > 0) {
                try {
                    var args = JSON.parse(argsJson)
                    page = args.page || args.name || ""
                } catch (e) { /* ignore */ }
            }
            if (page && page.length > 0)
                window.goToPage(page)
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
