import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import Polymath

ApplicationWindow {
    id: window
    width: 1280; height: 840
    minimumWidth: 960; minimumHeight: 600
    visible: true
    title: "Hearth — Local AI Home Assistant"
    color: Style.bg

    // Bundle Inter (SIL OFL) app-wide. This also silences the
    // "QFontDatabase: Cannot find font directory" warning by guaranteeing a
    // registered family, and makes headless renders match the desktop.
    FontLoader {
        id: inter
        source: "qrc:/qt/qml/Polymath/fonts/Inter.ttf"
        onStatusChanged: if (status === FontLoader.Ready) Style.fontFamily = inter.font.family
    }
    Component.onCompleted: {
        if (inter.status === FontLoader.Ready) Style.fontFamily = inter.font.family
        pageIndex = Math.max(0, Math.min(pageIndex, pages.length - 1))
    }
    font.family: Style.fontFamily

    // Six daily-use pages + one Settings hub (Personalities, Models, Privacy
    // and Mobile Access live inside it) — the rail stays short; the command
    // palette (Ctrl+K) deep-links everywhere for power users.
    readonly property var pages: [
        { name: "Dashboard", icon: "home",     src: "Dashboard.qml" },
        { name: "Chat",      icon: "chat",     src: "ChatView.qml" },
        { name: "Cameras",   icon: "camera",   src: "CamerasView.qml" },
        { name: "Tasks",     icon: "tasks",    src: "TaskQueueView.qml" },
        { name: "Timeline",  icon: "clock",    src: "TimelineView.qml" },
        { name: "Shopping",  icon: "cart",     src: "ShoppingView.qml" },
        { name: "Lab",       icon: "flask",    src: "LabView.qml" },
        { name: "Settings",  icon: "settings", src: "SettingsView.qml" }
    ]

    property int  pageIndex: 0
    property bool railCollapsed: false

    Settings {
        category: "shell"
        property alias pageIndex: window.pageIndex
        property alias railCollapsed: window.railCollapsed
    }

    function navigate(name) {
        for (var i = 0; i < pages.length; ++i)
            if (pages[i].name === name) { pageIndex = i; return }
    }

    // Views raise Nav signals (they cannot see into this file); react here.
    Connections {
        target: Nav
        function onNavigate(page) { window.navigate(page) }
        function onNotify(level, source, message) { window.pushToast(level, source, message) }
        function onFocusChat() { window.navigate("Chat") }
    }

    // --- keyboard: Ctrl+1..7 pages, Ctrl+K palette, Ctrl+B rail, Ctrl+, settings
    Instantiator {
        model: window.pages.length
        delegate: Shortcut {
            sequence: "Ctrl+" + (index + 1)
            onActivated: window.pageIndex = index
        }
    }
    Shortcut { sequence: "Ctrl+K"; onActivated: palette.open() }
    Shortcut { sequence: "Ctrl+B"; onActivated: window.railCollapsed = !window.railCollapsed }
    Shortcut { sequence: "Ctrl+,"; onActivated: window.navigate("Settings") }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- Navigation rail (collapsible: Ctrl+B or the chevron) ---
        Rectangle {
            id: rail
            Layout.fillHeight: true
            Layout.preferredWidth: window.railCollapsed ? Style.railWidthCollapsed : Style.railWidth
            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: Style.durMed; easing.type: Easing.OutCubic }
            }
            color: Style.surface
            clip: true
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Style.border }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4

                // Brand row — wordmark when expanded, monogram when collapsed.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 6
                    spacing: 8
                    Rectangle {
                        width: 28; height: 28; radius: 8
                        color: Style.accentDim
                        PmIcon { anchors.centerIn: parent; width: 16; height: 16; name: "sparkle"; color: Style.accent }
                    }
                    Label {
                        visible: !window.railCollapsed
                        text: "HEARTH"; color: Style.accent
                        font.family: Style.fontFamily
                        font.pixelSize: 16; font.bold: true; font.letterSpacing: 2
                        Layout.fillWidth: true
                    }
                }

                // Listening / idle affordance — a pulsing dot + state line.
                Rectangle {
                    Layout.fillWidth: true
                    radius: Style.radiusSm
                    color: Style.surface2
                    implicitHeight: 40
                    border.width: 1
                    border.color: app.listening ? Style.good : Style.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: window.railCollapsed ? 0 : 10
                        anchors.rightMargin: window.railCollapsed ? 0 : 10
                        spacing: 8
                        Item { visible: window.railCollapsed; Layout.fillWidth: true }
                        Rectangle {
                            width: 9; height: 9; radius: 4.5
                            color: app.listening ? Style.good : Style.textFaint
                            Layout.alignment: Qt.AlignVCenter
                            SequentialAnimation on opacity {
                                running: app.listening; loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 700; easing.type: Easing.InOutQuad }
                                NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutQuad }
                            }
                        }
                        ColumnLayout {
                            visible: !window.railCollapsed
                            spacing: 0; Layout.fillWidth: true
                            Label {
                                text: app.listening ? "Listening" : "Idle"
                                color: app.listening ? Style.good : Style.textDim
                                font.family: Style.fontFamily; font.pixelSize: Style.fsSmall; font.bold: true
                            }
                            Label {
                                text: app.activePersonality
                                color: Style.textFaint; font.family: Style.fontFamily
                                font.pixelSize: Style.fsTiny; elide: Text.ElideRight; Layout.fillWidth: true
                            }
                        }
                        Item { visible: window.railCollapsed; Layout.fillWidth: true }
                    }
                }

                Item { implicitHeight: 6 }

                // Page items.
                Repeater {
                    model: window.pages
                    delegate: AbstractButton {
                        id: navItem
                        required property var modelData
                        required property int index
                        readonly property bool current: window.pageIndex === index
                        Layout.fillWidth: true
                        implicitHeight: 34
                        onClicked: window.pageIndex = index

                        // Settings sits visually apart from the daily pages.
                        Layout.topMargin: modelData.name === "Settings" ? 10 : 0

                        background: Rectangle {
                            radius: Style.radiusSm
                            color: navItem.current ? Style.accentDim
                                 : navItem.hovered ? Style.surface2 : "transparent"
                            Behavior on color { ColorAnimation { duration: Style.durFast } }
                            Rectangle {
                                visible: navItem.current
                                anchors.left: parent.left; anchors.leftMargin: 2
                                anchors.verticalCenter: parent.verticalCenter
                                width: 3; height: 16; radius: 2; color: Style.accent
                            }
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            Item { visible: window.railCollapsed; Layout.fillWidth: true }
                            PmIcon {
                                Layout.leftMargin: window.railCollapsed ? 0 : 8
                                width: 17; height: 17
                                name: navItem.modelData.icon
                                color: navItem.current ? Style.accent
                                     : navItem.hovered ? Style.text : Style.textDim
                            }
                            Label {
                                visible: !window.railCollapsed
                                text: navItem.modelData.name
                                font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                                color: navItem.current ? Style.accent
                                     : navItem.hovered ? Style.text : Style.textDim
                                verticalAlignment: Text.AlignVCenter
                                Layout.fillWidth: true
                            }
                            Item { visible: window.railCollapsed; Layout.fillWidth: true }
                        }

                        ToolTip {
                            visible: navItem.hovered && window.railCollapsed
                            delay: 400
                            text: navItem.modelData.name + "   Ctrl+" + (navItem.index + 1)
                            contentItem: Text {
                                text: navItem.modelData.name + "   Ctrl+" + (navItem.index + 1)
                                color: Style.text; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                            }
                            background: Rectangle {
                                radius: Style.radiusXs; color: Style.surface3
                                border.width: 1; border.color: Style.border
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // Model status — one faint line; click jumps to Settings ▸ Models.
                AbstractButton {
                    Layout.fillWidth: true
                    visible: !window.railCollapsed
                    implicitHeight: statusLbl.implicitHeight + 8
                    onClicked: Nav.goSettings("Models")
                    background: Rectangle {
                        radius: Style.radiusXs
                        color: parent.hovered ? Style.surface2 : "transparent"
                    }
                    contentItem: Label {
                        id: statusLbl
                        text: app.modelStatus; color: app.hasModels ? Style.textFaint : Style.warn
                        font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                        elide: Text.ElideRight
                        leftPadding: 4
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Push-to-talk — turns green while held.
                AbstractButton {
                    id: ptt
                    Layout.fillWidth: true
                    implicitHeight: Style.controlH
                    onPressed: app.pushToTalk(true)
                    onReleased: app.pushToTalk(false)
                    background: Rectangle {
                        radius: Style.radiusSm
                        color: ptt.down ? Style.accent : Style.surface2
                        border.width: 1
                        border.color: ptt.down ? Style.accent : Style.border
                        Behavior on color { ColorAnimation { duration: Style.durFast } }
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        Item { Layout.fillWidth: true }
                        PmIcon {
                            width: 16; height: 16; name: "mic"
                            color: ptt.down ? Style.accentText : Style.textDim
                        }
                        Label {
                            visible: !window.railCollapsed
                            text: ptt.down ? "Listening…" : "Push to talk"
                            color: ptt.down ? Style.accentText : Style.text
                            font.family: Style.fontFamily; font.pixelSize: Style.fsBody
                        }
                        Item { Layout.fillWidth: true }
                    }
                    ToolTip {
                        visible: ptt.hovered && window.railCollapsed
                        delay: 400
                        contentItem: Text {
                            text: "Push to talk"
                            color: Style.text; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                        }
                        background: Rectangle {
                            radius: Style.radiusXs; color: Style.surface3
                            border.width: 1; border.color: Style.border
                        }
                    }
                }

                // Footer: palette hint + collapse toggle.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    spacing: 4
                    AbstractButton {
                        id: paletteHint
                        visible: !window.railCollapsed
                        Layout.fillWidth: true
                        implicitHeight: 26
                        onClicked: palette.open()
                        background: Rectangle {
                            radius: Style.radiusXs
                            color: paletteHint.hovered ? Style.surface2 : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 6
                            PmIcon { width: 13; height: 13; name: "search"; color: Style.textFaint; Layout.leftMargin: 6 }
                            Label {
                                text: "Search"; color: Style.textFaint
                                font.family: Style.fontFamily; font.pixelSize: Style.fsTiny
                                Layout.fillWidth: true
                            }
                            Rectangle {
                                radius: 4; color: Style.surface2; border.width: 1; border.color: Style.border
                                implicitWidth: kbd.implicitWidth + 10; implicitHeight: 18
                                Label {
                                    id: kbd; anchors.centerIn: parent
                                    text: "Ctrl K"; color: Style.textFaint
                                    font.family: Style.fontFamily; font.pixelSize: 10
                                }
                            }
                        }
                    }
                    Item { visible: window.railCollapsed; Layout.fillWidth: true }
                    PmIconButton {
                        glyph: window.railCollapsed ? "chevron-right" : "chevron-left"
                        tip: (window.railCollapsed ? "Expand" : "Collapse") + "  Ctrl+B"
                        onClicked: window.railCollapsed = !window.railCollapsed
                    }
                    Item { visible: window.railCollapsed; Layout.fillWidth: true }
                }
            }
        }

        // --- Page area (lazy: a page loads on first visit, then stays warm) ---
        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: window.pageIndex
            Repeater {
                model: window.pages
                delegate: Loader {
                    id: pageLoader
                    required property var modelData
                    required property int index
                    property bool everActive: false
                    source: modelData.src
                    // Bind `active` to the latch only -> no binding loop.  The latch
                    // is set imperatively the first time this page becomes current
                    // (below), then never clears, so the page stays warm.
                    active: everActive
                    function latchIfCurrent() { if (stack.currentIndex === index) everActive = true }
                    Component.onCompleted: latchIfCurrent()
                    Connections {
                        target: stack
                        function onCurrentIndexChanged() { pageLoader.latchIfCurrent() }
                    }

                    // Soft entrance when a page becomes current: fade up + rise a
                    // few px into place.  StackLayout shows only the current item,
                    // so this reads as a gentle cross-cut rather than a hard swap.
                    opacity: StackLayout.isCurrentItem ? 1 : 0
                    Behavior on opacity {
                        NumberAnimation { duration: Style.durMed; easing.type: Style.easeStandard }
                    }
                    property real slide: StackLayout.isCurrentItem ? 0 : 10
                    Behavior on slide {
                        NumberAnimation { duration: Style.durMed; easing.type: Style.easeStandard }
                    }
                    transform: Translate { y: slide }
                }
            }
        }
    }

    CommandPalette {
        id: palette
        anchors.centerIn: undefined
        x: Math.round((window.width - width) / 2)
        y: 96
    }

    // --- Computer use: glowing screen border (separate click-through window) +
    //     an in-window panic-stop banner so the user can always halt it. ---
    ControlOverlay { }

    // --- Global quick-ask pop-over (Ctrl+Alt+Space), a separate focusable
    //     top-level window so it can float over any app. ---
    QuickAsk { }

    Rectangle {
        visible: app.controlling
        z: 1000
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 14 }
        radius: Style.radiusSm
        color: Style.surface3
        border.width: 1; border.color: Style.warn
        implicitWidth: ctlRow.implicitWidth + 24
        implicitHeight: 40
        RowLayout {
            id: ctlRow
            anchors.centerIn: parent
            spacing: 10
            Rectangle {
                width: 9; height: 9; radius: 4.5; color: Style.warn
                Layout.alignment: Qt.AlignVCenter
                SequentialAnimation on opacity {
                    running: app.controlling; loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 500 }
                    NumberAnimation { to: 1.0; duration: 500 }
                }
            }
            Label {
                text: (app.controlAction && app.controlAction.length)
                      ? ("Controlling: " + app.controlAction)
                      : "Hearth is controlling the computer"
                color: Style.text; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                elide: Text.ElideRight; Layout.maximumWidth: 360
            }
            PmButton { text: "Stop"; onClicked: app.stopControl() }
        }
    }

    // --- Toasts: stacked, dismissible, auto-expiring ---
    function pushToast(level, source, message) {
        if (toastModel.count >= 4) toastModel.remove(0)
        toastModel.append({ level: level, source: source, message: message })
    }
    Connections {
        target: app
        function onNoticePosted(level, source, message) { window.pushToast(level, source, message) }
    }
    ListModel { id: toastModel }
    ColumnLayout {
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 20 }
        spacing: 8
        Repeater {
            model: toastModel
            delegate: Rectangle {
                id: toast
                required property int index
                required property string level
                required property string source
                required property string message
                readonly property color tone: level === "error" ? Style.bad
                                            : level === "warn"  ? Style.warn
                                            : level === "good"  ? Style.good : Style.accent
                Layout.alignment: Qt.AlignHCenter
                width: Math.min(toastRow.implicitWidth + 24, window.width - 80)
                height: 40; radius: Style.radiusSm; color: Style.surface3
                border.width: 1; border.color: Style.border
                opacity: 0
                Component.onCompleted: opacity = 1
                Behavior on opacity { NumberAnimation { duration: Style.durMed } }

                RowLayout {
                    id: toastRow
                    anchors.fill: parent
                    anchors.leftMargin: 12; anchors.rightMargin: 6
                    spacing: 10
                    Rectangle { width: 6; height: 18; radius: 3; color: toast.tone }
                    Label {
                        text: (toast.source.length ? toast.source + ":  " : "") + toast.message
                        color: Style.text; font.family: Style.fontFamily; font.pixelSize: Style.fsSmall
                        elide: Text.ElideRight; Layout.fillWidth: true
                    }
                    PmIconButton {
                        glyph: "x"
                        implicitWidth: 24; implicitHeight: 24
                        onClicked: if (toast.index < toastModel.count) toastModel.remove(toast.index)
                    }
                }
                Timer {
                    interval: 4500 + toast.index * 300; running: true
                    onTriggered: if (toast.index < toastModel.count) toastModel.remove(toast.index)
                }
            }
        }
    }
}
