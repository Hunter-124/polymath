import QtQuick
import QtQuick.Layouts
import QtWebEngine
import Polymath

// WebSurface — real QtWebEngine embed (D5): shared adblock profile + YouTube
// clean mode. B2: video mode prefers a youtube-nocookie.com embed (fewer ads/
// overlays than a watch page) for videoId spawns / youtube watch URLs; keeps
// support for arbitrary web pages. Clean-mode script is fetched from the qrc
// resource YtClean.js (B3) instead of an inline string — see
// docs/overhaul2/results/B3_contract.md for the injection contract.
GlassCard {
    id: root
    property string title: "Web"
    property string url: ""
    property string videoId: ""
    property string argsJson: ""
    property string surfaceId: ""
    property string mode: "page"   // page | video (YouTube clean)

    // Emitted so SurfaceHost can close/replace this surface's slot; SurfaceHost
    // connects these dynamically in its Loader.onLoaded (same pattern as
    // VideoPickerSurface.requestSpawn).
    signal requestClose()

    section: "Chat"
    implicitWidth: 480
    implicitHeight: 300
    clip: true

    readonly property var args: {
        if (!argsJson || argsJson.length === 0) return ({})
        try { return JSON.parse(argsJson) } catch (e) { return ({}) }
    }
    readonly property string resolvedUrl: (args && args.url) ? args.url : root.url
    readonly property string resolvedMode: (args && args.mode) ? args.mode
                                          : (resolvedVideoId.length > 0 ? "video" : root.mode)
    readonly property string displayTitle: (args && args.title && args.title.length > 0)
                                          ? args.title : root.title

    // videoId can arrive directly (args.videoId / root.videoId) or be embedded
    // in a youtube.com/watch, youtu.be, or /embed/ URL — extract it either way.
    readonly property string resolvedVideoId: {
        if (args && args.videoId && args.videoId.length > 0) return args.videoId
        if (root.videoId.length > 0) return root.videoId
        var u = resolvedUrl || ""
        var m = u.match(/[?&]v=([A-Za-z0-9_-]{6,})/)
        if (m) return m[1]
        m = u.match(/youtu\.be\/([A-Za-z0-9_-]{6,})/)
        if (m) return m[1]
        m = u.match(/youtube(?:-nocookie)?\.com\/embed\/([A-Za-z0-9_-]{6,})/)
        if (m) return m[1]
        return ""
    }

    // Prefer the nocookie embed (fewer ads/overlays than a watch page) whenever
    // we have a resolvable video id; otherwise fall back to whatever URL/page
    // was requested.
    readonly property string embedUrl: resolvedVideoId.length > 0
        ? ("https://www.youtube-nocookie.com/embed/" + resolvedVideoId + "?autoplay=1&rel=0")
        : ""
    readonly property string effectiveUrl: embedUrl.length > 0 ? embedUrl : resolvedUrl

    readonly property bool isYouTube: {
        if (resolvedVideoId.length > 0) return true
        var u = (effectiveUrl || "").toLowerCase()
        return u.indexOf("youtube.com") >= 0 || u.indexOf("youtu.be") >= 0
    }

    // B3 contract: fetch the standalone clean-mode script from its qrc path
    // once, cache the text, and runJavaScript() it from the existing
    // loading/url-changed call sites. Idempotent (window.__pmYtCleanInstalled
    // guard inside the script) — safe, and expected, to re-inject on every
    // load / soft-nav.
    property string ytCleanScript: ""

    function injectClean() {
        if ((root.isYouTube || root.resolvedMode === "video") && root.ytCleanScript.length > 0)
            web.runJavaScript(root.ytCleanScript)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.gapSm
        spacing: Style.gapSm

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gapSm
            Text {
                text: root.displayTitle
                color: Style.text
                font.family: Style.fontFamily
                font.pixelSize: Style.fsSmall
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            PmPill {
                text: root.isYouTube ? "YouTube" : "Web"
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            WebEngineView {
                id: web
                anchors.fill: parent
                url: root.effectiveUrl.length > 0 ? root.effectiveUrl : "about:blank"
                settings.javascriptEnabled: true
                settings.pluginsEnabled: true
                settings.playbackRequiresUserGesture: false
                settings.localContentCanAccessRemoteUrls: true

                onLoadingChanged: function(loadRequest) {
                    if (loadRequest.status === WebEngineView.LoadSucceededStatus)
                        root.injectClean()
                }
                onUrlChanged: Qt.callLater(root.injectClean)
            }

            Rectangle {
                anchors.fill: parent
                color: Style.bgDeep
                opacity: web.loading ? 0.55 : 0
                visible: opacity > 0.01
                Behavior on opacity { NumberAnimation { duration: 120 } }
                Text {
                    anchors.centerIn: parent
                    text: "Loading..."
                    color: Style.textDim
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fsBody
                }
            }
        }

        // Control row: back / mute / close (video-player-style transport row;
        // useful for arbitrary web pages too, so it's not gated to video mode).
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gapSm

            PmButton {
                text: "◀ Back"
                flat: true
                enabled: web.canGoBack
                onClicked: web.goBack()
            }
            PmButton {
                text: web.audioMuted ? "Unmute" : "Mute"
                flat: true
                onClicked: web.audioMuted = !web.audioMuted
            }
            Item { Layout.fillWidth: true }
            PmButton {
                text: "Close"
                flat: true
                onClicked: root.requestClose()
            }
        }
    }

    Component.onCompleted: {
        if (args.mode) root.mode = args.mode
        if (args.url && (!root.url || root.url.length === 0))
            root.url = args.url
        if (args.videoId && root.videoId.length === 0)
            root.videoId = args.videoId

        var xhr = new XMLHttpRequest()
        xhr.open("GET", "qrc:/qt/qml/Polymath/qml/surfaces/YtClean.js")
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE && xhr.status === 200) {
                root.ytCleanScript = xhr.responseText
                root.injectClean()
            }
        }
        xhr.send()
    }
}
