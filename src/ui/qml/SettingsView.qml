import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Polymath

// SettingsView — full settings surface bound to settings controller (02 §F1).
Item {
    id: root
    property string focusSection: ""

    // Accent swatch palette (product hues)
    readonly property var accentSwatches: [
        "#33E1FF", "#4C8CFF", "#2BD9C6", "#FFB23E",
        "#9B6BFF", "#46E08A", "#FF6BD0", "#6D7BFF",
        "#FF5C6E", "#A8B4D8"
    ]

    function scrollToSection(key) {
        if (!key || key.length === 0) return
        // Map focusSection → y of matching SettingsSection
        var map = {
            "appearance": secAppearance,
            "audio": secAudio,
            "voice": secVoice,
            "search": secSearch,
            "web": secSearch,
            "behavior": secBehavior,
            "agents": secAgents,
            "privacy": secPrivacyLink
        }
        var target = map[key.toLowerCase()]
        if (target) {
            // Position flick so section is near top
            var y = target.mapToItem(scrollContent, 0, 0).y
            flick.contentY = Math.max(0, Math.min(y - Style.gap, flick.contentHeight - flick.height))
        }
    }

    onFocusSectionChanged: Qt.callLater(function () { root.scrollToSection(root.focusSection) })

    // Local mirrors for free-form keys (reload on settingChanged)
    property string wakeWord: settings.getString("audio.wake_word", "hey_jarvis")
    property string searchBackend: settings.getString("web.search_backend", "ddg")
    property string searchApiKey: settings.getString("web.search_api_key", "")
    property string quietStart: settings.getString("behavior.quiet_start", "22:00")
    property string quietEnd: settings.getString("behavior.quiet_end", "07:00")
    property int retainAmbient: settings.getInt("retention.ambient_days", 7)
    property int retainEvents: settings.getInt("retention.events_days", 30)
    property bool startMinimized: settings.getBool("app.start_minimized", false)
    property bool launchOnLogin: settings.getBool("app.launch_on_login", false)
    property string allowedDirs: settings.getString("agents.allowed_dirs", "")
    property int maxConcurrent: settings.getInt("agents.max_concurrent", 2)
    property string inputDevice: settings.getString("audio.input_device", "")
    property string outputDevice: settings.getString("audio.output_device", "")

    property var inputDevices: []
    property var outputDevices: []
    property var inputLabels: []
    property var outputLabels: []

    // Voice (D4): Kokoro voice picker + preview state.
    property var ttsVoiceList: []
    property var ttsVoiceIds: []
    property var ttsVoiceLabels: []
    property bool ttsPreviewing: false
    readonly property var ttsEngineIds: ["auto", "kokoro", "piper"]
    readonly property var ttsEngineLabels: ["Auto (Kokoro if installed)", "Kokoro (neural)", "Piper (fallback)"]

    function refreshTtsVoices() {
        ttsVoiceList = settings.ttsVoices() || []
        var ids = [], labels = []
        for (var i = 0; i < ttsVoiceList.length; ++i) {
            ids.push(ttsVoiceList[i].id)
            labels.push(ttsVoiceList[i].label)
        }
        ttsVoiceIds = ids
        ttsVoiceLabels = labels
    }
    function indexOfVoice(id) {
        var i = root.ttsVoiceIds.indexOf(id)
        return i >= 0 ? i : 0
    }

    function refreshDevices() {
        inputDevices = settings.audioInputDevices() || []
        outputDevices = settings.audioOutputDevices() || []
        inputLabels = deviceLabels(inputDevices)
        outputLabels = deviceLabels(outputDevices)
    }
    function deviceLabels(list) {
        var out = ["System default"]
        for (var i = 0; i < list.length; ++i) {
            var d = list[i]
            out.push(d.label || d.id || ("Device " + i))
        }
        return out
    }
    function deviceIdAt(list, index) {
        if (index <= 0) return ""
        var d = list[index - 1]
        return d ? (d.id || "") : ""
    }
    function indexOfDevice(list, id) {
        if (!id || id.length === 0) return 0
        for (var i = 0; i < list.length; ++i) {
            if ((list[i].id || "") === id) return i + 1
        }
        return 0
    }

    Component.onCompleted: {
        refreshDevices()
        refreshTtsVoices()
        if (focusSection.length > 0)
            Qt.callLater(function () { root.scrollToSection(root.focusSection) })
    }

    Timer {
        id: ttsPreviewResetTimer
        interval: 4000
        onTriggered: root.ttsPreviewing = false
    }

    Connections {
        target: settings
        function onSettingChanged(key, value) {
            // Refresh mirrored free-form fields when external writers touch them
            if (key === "audio.wake_word") root.wakeWord = String(value)
            else if (key === "web.search_backend") root.searchBackend = String(value)
            else if (key === "web.search_api_key") root.searchApiKey = String(value)
            else if (key === "behavior.quiet_start") root.quietStart = String(value)
            else if (key === "behavior.quiet_end") root.quietEnd = String(value)
            else if (key === "retention.ambient_days") root.retainAmbient = Number(value)
            else if (key === "retention.events_days") root.retainEvents = Number(value)
            else if (key === "app.start_minimized") root.startMinimized = (value === true || value === 1 || value === "1")
            else if (key === "app.launch_on_login") root.launchOnLogin = (value === true || value === 1 || value === "1")
            else if (key === "agents.allowed_dirs") root.allowedDirs = String(value)
            else if (key === "agents.max_concurrent") root.maxConcurrent = Number(value)
            else if (key === "audio.input_device") root.inputDevice = String(value)
            else if (key === "audio.output_device") root.outputDevice = String(value)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.pad
        spacing: Style.gap

        PmSectionHeader {
            Layout.fillWidth: true
            title: "Settings"
            section: "Settings"
            subtitle: "Appearance · Audio · Voice · Search · Behavior · Agents"
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: scrollContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: PmScrollBar { tone: Style.sectionColor("Settings") }

            ColumnLayout {
                id: scrollContent
                width: parent.width
                spacing: Style.gapLg

                // ---------- Appearance ----------
                SettingsSection {
                    id: secAppearance
                    sectionKey: "appearance"
                    title: "Appearance"
                    Layout.fillWidth: true

                    ColumnLayout {
                        width: parent.width
                        spacing: Style.gap

                        Text {
                            text: "Accent colour"
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Style.gapSm
                            Repeater {
                                model: root.accentSwatches
                                delegate: Rectangle {
                                    required property string modelData
                                    width: 28; height: 28; radius: 14
                                    color: modelData
                                    border.width: settings.accent.toLowerCase() === modelData.toLowerCase() ? 2 : 1
                                    border.color: settings.accent.toLowerCase() === modelData.toLowerCase()
                                                  ? Style.text : Style.glassBorder
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: settings.accent = modelData
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Style.gap
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: "Glass effects"
                                    color: Style.text
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsBody
                                }
                                Text {
                                    text: "Blur and ambient glow when the GPU path is available"
                                    color: Style.textFaint
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsTiny
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                            PmSwitch {
                                checked: settings.effects
                                onToggled: settings.effects = checked
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            enabled: settings.effects
                            opacity: settings.effects ? 1 : 0.45
                            Text {
                                text: "Effects intensity  " + Math.round(settings.effectsIntensity * 100) + "%"
                                color: Style.textDim
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsSmall
                            }
                            PmSlider {
                                Layout.fillWidth: true
                                from: 0; to: 1
                                value: settings.effectsIntensity
                                onMoved: settings.effectsIntensity = value
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: "Font scale  " + settings.fontScale.toFixed(2) + "x"
                                color: Style.textDim
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsSmall
                            }
                            PmSlider {
                                Layout.fillWidth: true
                                from: 0.85; to: 1.35
                                value: settings.fontScale
                                onMoved: settings.fontScale = value
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Style.gap
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: "Reduce motion"
                                    color: Style.text
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsBody
                                }
                                Text {
                                    text: "Prefer fades over slide and scale animations"
                                    color: Style.textFaint
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsTiny
                                }
                            }
                            PmSwitch {
                                checked: settings.reduceMotion
                                onToggled: settings.reduceMotion = checked
                            }
                        }
                    }
                }

                // ---------- Audio ----------
                SettingsSection {
                    id: secAudio
                    sectionKey: "audio"
                    title: "Audio"
                    Layout.fillWidth: true

                    ColumnLayout {
                        width: parent.width
                        spacing: Style.gap

                        Text {
                            text: "Input device"
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                        }
                        PmComboBox {
                            Layout.fillWidth: true
                            model: root.inputLabels
                            currentIndex: root.indexOfDevice(root.inputDevices, root.inputDevice)
                            onActivated: {
                                var id = root.deviceIdAt(root.inputDevices, currentIndex)
                                root.inputDevice = id
                                settings.setString("audio.input_device", id)
                            }
                        }

                        Text {
                            text: "Output device"
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                        }
                        PmComboBox {
                            Layout.fillWidth: true
                            model: root.outputLabels
                            currentIndex: root.indexOfDevice(root.outputDevices, root.outputDevice)
                            onActivated: {
                                var id = root.deviceIdAt(root.outputDevices, currentIndex)
                                root.outputDevice = id
                                settings.setString("audio.output_device", id)
                            }
                        }

                        Text {
                            text: "Wake word phrase"
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                        }
                        PmTextField {
                            Layout.fillWidth: true
                            text: root.wakeWord
                            placeholderText: "hey_jarvis"
                            onEditingFinished: {
                                root.wakeWord = text
                                settings.setString("audio.wake_word", text)
                            }
                        }
                    }
                }

                // ---------- Voice (D4) ----------
                SettingsSection {
                    id: secVoice
                    sectionKey: "voice"
                    title: "Voice"
                    Layout.fillWidth: true

                    ColumnLayout {
                        width: parent.width
                        spacing: Style.gap

                        Text {
                            text: "TTS engine"
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                        }
                        PmComboBox {
                            id: engineBox
                            Layout.fillWidth: true
                            model: root.ttsEngineLabels
                            Component.onCompleted: {
                                var i = root.ttsEngineIds.indexOf(settings.ttsEngine)
                                currentIndex = i >= 0 ? i : 0
                            }
                            onActivated: {
                                if (currentIndex >= 0 && currentIndex < root.ttsEngineIds.length)
                                    settings.ttsEngine = root.ttsEngineIds[currentIndex]
                            }
                        }
                        Text {
                            text: "Engine changes take effect next time Polymath starts."
                            color: Style.textFaint
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsTiny
                        }

                        Text {
                            text: "Voice"
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                        }
                        PmComboBox {
                            id: voiceBox
                            Layout.fillWidth: true
                            model: root.ttsVoiceLabels
                            currentIndex: root.indexOfVoice(settings.ttsVoice)
                            onActivated: {
                                if (currentIndex >= 0 && currentIndex < root.ttsVoiceIds.length)
                                    settings.ttsVoice = root.ttsVoiceIds[currentIndex]
                            }
                        }
                        Text {
                            text: "Personas with their own voice field override this default."
                            color: Style.textFaint
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsTiny
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: "Speed  " + settings.ttsSpeed.toFixed(2) + "x"
                                color: Style.textDim
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsSmall
                            }
                            PmSlider {
                                Layout.fillWidth: true
                                from: 0.8; to: 1.3
                                value: settings.ttsSpeed
                                onMoved: settings.ttsSpeed = value
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: "Volume  " + Math.round(settings.ttsVolume * 100) + "%"
                                color: Style.textDim
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsSmall
                            }
                            PmSlider {
                                Layout.fillWidth: true
                                from: 0; to: 1.5
                                value: settings.ttsVolume
                                onMoved: settings.ttsVolume = value
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Style.gap
                            PmButton {
                                text: root.ttsPreviewing ? "Playing…" : "Preview voice"
                                enabled: !root.ttsPreviewing
                                tone: Style.sectionColor("Settings")
                                onClicked: {
                                    root.ttsPreviewing = true
                                    settings.previewVoice("")
                                    ttsPreviewResetTimer.restart()
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }

                // ---------- Web Search ----------
                SettingsSection {
                    id: secSearch
                    sectionKey: "search"
                    title: "Web Search"
                    Layout.fillWidth: true

                    ColumnLayout {
                        width: parent.width
                        spacing: Style.gap

                        Text {
                            text: "Backend"
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                        }
                        PmComboBox {
                            id: backendBox
                            Layout.fillWidth: true
                            model: ["ddg", "searxng", "brave"]
                            Component.onCompleted: {
                                var i = model.indexOf(root.searchBackend)
                                if (i >= 0) currentIndex = i
                            }
                            onActivated: {
                                root.searchBackend = model[currentIndex]
                                settings.setString("web.search_backend", root.searchBackend)
                            }
                        }

                        Text {
                            text: "API key (Brave / SearXNG auth)"
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                        }
                        PmTextField {
                            Layout.fillWidth: true
                            text: root.searchApiKey
                            placeholderText: "optional"
                            echoMode: TextInput.Password
                            onEditingFinished: {
                                root.searchApiKey = text
                                settings.setString("web.search_api_key", text)
                            }
                        }
                    }
                }

                // ---------- Behavior ----------
                SettingsSection {
                    id: secBehavior
                    sectionKey: "behavior"
                    title: "Behavior"
                    Layout.fillWidth: true

                    ColumnLayout {
                        width: parent.width
                        spacing: Style.gap

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Style.gap
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: "Quiet hours start"
                                    color: Style.textDim
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsSmall
                                }
                                PmTextField {
                                    Layout.fillWidth: true
                                    text: root.quietStart
                                    placeholderText: "22:00"
                                    onEditingFinished: {
                                        root.quietStart = text
                                        settings.setString("behavior.quiet_start", text)
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: "Quiet hours end"
                                    color: Style.textDim
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsSmall
                                }
                                PmTextField {
                                    Layout.fillWidth: true
                                    text: root.quietEnd
                                    placeholderText: "07:00"
                                    onEditingFinished: {
                                        root.quietEnd = text
                                        settings.setString("behavior.quiet_end", text)
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Style.gap
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: "Retain ambient (days)"
                                    color: Style.textDim
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsSmall
                                }
                                PmTextField {
                                    Layout.fillWidth: true
                                    text: String(root.retainAmbient)
                                    placeholderText: "7"
                                    onEditingFinished: {
                                        var n = parseInt(text, 10)
                                        if (isNaN(n) || n < 0) n = 7
                                        root.retainAmbient = n
                                        settings.setInt("retention.ambient_days", n)
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: "Retain events (days)"
                                    color: Style.textDim
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fsSmall
                                }
                                PmTextField {
                                    Layout.fillWidth: true
                                    text: String(root.retainEvents)
                                    placeholderText: "30"
                                    onEditingFinished: {
                                        var n = parseInt(text, 10)
                                        if (isNaN(n) || n < 0) n = 30
                                        root.retainEvents = n
                                        settings.setInt("retention.events_days", n)
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "Start minimized"
                                color: Style.text
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsBody
                                Layout.fillWidth: true
                            }
                            PmSwitch {
                                checked: root.startMinimized
                                onToggled: {
                                    root.startMinimized = checked
                                    settings.setBool("app.start_minimized", checked)
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "Launch on login"
                                color: Style.text
                                font.family: Style.fontFamily
                                font.pixelSize: Style.fsBody
                                Layout.fillWidth: true
                            }
                            PmSwitch {
                                checked: root.launchOnLogin
                                onToggled: {
                                    root.launchOnLogin = checked
                                    settings.setBool("app.launch_on_login", checked)
                                }
                            }
                        }
                    }
                }

                // ---------- Agents ----------
                SettingsSection {
                    id: secAgents
                    sectionKey: "agents"
                    title: "Agents"
                    Layout.fillWidth: true

                    ColumnLayout {
                        width: parent.width
                        spacing: Style.gap

                        Text {
                            text: "Allowed directories (semicolon-separated)"
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                        }
                        PmTextField {
                            Layout.fillWidth: true
                            text: root.allowedDirs
                            placeholderText: "C:\\Users\\…\\Documents;D:\\Projects"
                            onEditingFinished: {
                                root.allowedDirs = text
                                settings.setString("agents.allowed_dirs", text)
                            }
                        }

                        Text {
                            text: "Max concurrent agents"
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsSmall
                        }
                        PmComboBox {
                            Layout.preferredWidth: 120
                            model: ["1", "2", "3", "4"]
                            currentIndex: Math.max(0, Math.min(3, root.maxConcurrent - 1))
                            onActivated: {
                                var n = parseInt(model[currentIndex], 10)
                                root.maxConcurrent = n
                                settings.setInt("agents.max_concurrent", n)
                            }
                        }
                    }
                }

                // ---------- Privacy link ----------
                SettingsSection {
                    id: secPrivacyLink
                    sectionKey: "privacy"
                    title: "Privacy"
                    Layout.fillWidth: true

                    ColumnLayout {
                        width: parent.width
                        spacing: Style.gapSm
                        Text {
                            text: "Master sense toggles (mic, cameras, face recognition, encryption) live on the Privacy page — they keep first-run opt-in semantics."
                            color: Style.textDim
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fsBody
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        PmButton {
                            text: "Open Privacy"
                            flat: true
                            tone: Style.sectionColor("Privacy")
                            // C1 can rebind to window.goToPage("Privacy")
                            onClicked: {
                                if (typeof window !== "undefined" && window.goToPage)
                                    window.goToPage("Privacy")
                            }
                        }
                    }
                }

                Item { Layout.preferredHeight: Style.pad }
            }
        }
    }
}
