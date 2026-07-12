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
//
// IMPORTANT: content is rendered INSIDE this glass card (not a separate OS
// browser window). The AI positions cards via SurfaceHost / ui_control x,y,w,h.
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

    // Direct /embed/ top-level navigations in WebEngine hit YouTube Error 153
    // (missing client identity / referrer). We host a tiny HTML shell under a
    // youtube-nocookie origin via loadHtml(baseUrl) and put the real player in
    // an <iframe referrerpolicy=...>. See WebAdblockInterceptor Referer stamp.
    readonly property string embedSrc: resolvedVideoId.length > 0
        ? ("https://www.youtube-nocookie.com/embed/" + resolvedVideoId
           + "?autoplay=1&rel=0&modestbranding=1&playsinline=1&enablejsapi=1"
           + "&origin=" + encodeURIComponent("https://www.youtube-nocookie.com"))
        : ""
    readonly property string embedUrl: embedSrc
    readonly property string effectiveUrl: embedSrc.length > 0 ? embedSrc : resolvedUrl
    property bool usingHtmlPlayer: false

    readonly property bool isYouTube: {
        if (resolvedVideoId.length > 0) return true
        var u = (effectiveUrl || "").toLowerCase()
        return u.indexOf("youtube.com") >= 0 || u.indexOf("youtu.be") >= 0
                || u.indexOf("youtube-nocookie.com") >= 0
    }

    // B3 contract: fetch the standalone clean-mode script from its qrc path
    // once, cache the text, and runJavaScript() it from the existing
    // loading/url-changed call sites. Idempotent (window.__pmYtCleanInstalled
    // guard inside the script) — safe, and expected, to re-inject on every
    // load / soft-nav.
    property string ytCleanScript: ""
    property string loadError: ""
    property int loadProgress: 0
    property bool hasLoadedOnce: false

    function injectClean() {
        // Skip about:blank and our HTML player shell (iframe is cross-origin;
        // adblock + referrer fix handle most of that path). Clean-mode still
        // runs on full watch pages.
        if (root.usingHtmlPlayer) return
        if ((root.isYouTube || root.resolvedMode === "video") && root.ytCleanScript.length > 0
                && web && web.url && web.url.toString().indexOf("about:blank") < 0) {
            web.runJavaScript(root.ytCleanScript)
        }
    }

    function buildPlayerHtml(videoId) {
        var src = "https://www.youtube-nocookie.com/embed/" + videoId
                + "?autoplay=1&rel=0&modestbranding=1&playsinline=1&enablejsapi=1"
                + "&origin=" + encodeURIComponent("https://www.youtube-nocookie.com")
        return "<!DOCTYPE html><html><head>"
             + "<meta charset=\"utf-8\">"
             + "<meta name=\"referrer\" content=\"strict-origin-when-cross-origin\">"
             + "<style>"
             + "html,body{margin:0;height:100%;background:#0b0d12;overflow:hidden}"
             + "iframe{position:absolute;inset:0;width:100%;height:100%;border:0}"
             + "</style></head><body>"
             + "<iframe id=\"yt\" src=\"" + src + "\" title=\"YouTube\""
             + " allow=\"accelerometer; autoplay; clipboard-write; encrypted-media;"
             + " gyroscope; picture-in-picture; web-share\""
             + " allowfullscreen"
             + " referrerpolicy=\"strict-origin-when-cross-origin\""
             + "></iframe>"
             + "</body></html>"
    }

    function navigateTo(target) {
        root.loadError = ""
        root.usingHtmlPlayer = false
        var u = (target || "").trim()
        if (!u || u.length === 0) {
            web.url = "about:blank"
            return
        }
        // If the user pasted a YouTube watch/share URL, switch to the HTML player.
        var m = u.match(/[?&]v=([A-Za-z0-9_-]{6,})/)
                || u.match(/youtu\.be\/([A-Za-z0-9_-]{6,})/)
                || u.match(/youtube(?:-nocookie)?\.com\/embed\/([A-Za-z0-9_-]{6,})/)
        if (m && m[1]) {
            playVideoId(m[1])
            return
        }
        if (u.indexOf("://") < 0 && u.indexOf(".") >= 0)
            u = "https://" + u
        web.url = u
    }

    function playVideoId(videoId) {
        root.loadError = ""
        root.usingHtmlPlayer = true
        // baseUrl gives the document a youtube-nocookie origin so the iframe
        // player receives a valid Referer (avoids Error 153).
        web.loadHtml(buildPlayerHtml(videoId), "https://www.youtube-nocookie.com/")
    }

    function applyEffectiveUrl() {
        if (root.resolvedVideoId.length > 0) {
            playVideoId(root.resolvedVideoId)
            return
        }
        if (root.effectiveUrl && root.effectiveUrl.length > 0)
            navigateTo(root.effectiveUrl)
        else
            navigateTo("about:blank")
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

        // Address bar (web pages only — video embeds hide chrome for a player look)
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.gapSm
            visible: !root.isYouTube || root.resolvedMode === "page"
            PmTextField {
                id: urlField
                Layout.fillWidth: true
                text: root.effectiveUrl
                placeholderText: "https://"
                onAccepted: root.navigateTo(text)
            }
            PmButton {
                text: "Go"
                flat: true
                onClicked: root.navigateTo(urlField.text)
            }
        }

        Item {
            id: webHost
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Opaque host so WebEngine compositing isn't mixed with glass alpha
            // (common cause of blank Chromium surfaces on Windows / hybrid GPUs).
            clip: true

            Rectangle {
                anchors.fill: parent
                radius: Style.radiusSm
                color: "#0b0d12"
                border.width: 1
                border.color: Style.glassBorder
            }

            WebEngineView {
                id: web
                anchors.fill: parent
                anchors.margins: 1
                // Bind once; further navigations go through navigateTo / goBack.
                url: "about:blank"
                backgroundColor: "#0b0d12"
                settings.javascriptEnabled: true
                settings.playbackRequiresUserGesture: false
                settings.localContentCanAccessRemoteUrls: true
                settings.errorPageEnabled: true
                settings.focusOnNavigationEnabled: true

                onLoadingChanged: function(loadRequest) {
                    if (loadRequest.status === WebEngineView.LoadStartedStatus) {
                        root.loadError = ""
                        root.loadProgress = 0
                    } else if (loadRequest.status === WebEngineView.LoadSucceededStatus) {
                        root.hasLoadedOnce = true
                        root.loadError = ""
                        root.loadProgress = 100
                        root.injectClean()
                        if (urlField && !root.isYouTube)
                            urlField.text = web.url.toString()
                    } else if (loadRequest.status === WebEngineView.LoadFailedStatus) {
                        // Ignore empty/cancelled loads (rapid reloads / parent resize).
                        var es = (loadRequest.errorString || "").toLowerCase()
                        if (es.indexOf("cancel") >= 0 || es.indexOf("aborted") >= 0)
                            return
                        root.loadError = loadRequest.errorString
                                || ("Load failed (code " + loadRequest.errorCode + ")")
                        console.warn("[WebSurface] load failed:", loadRequest.errorString,
                                     loadRequest.errorCode, loadRequest.url)
                    } else if (loadRequest.status === WebEngineView.LoadStoppedStatus) {
                        // User/stop — keep whatever we had.
                    }
                }
                onUrlChanged: Qt.callLater(root.injectClean)
                onLoadingProgressChanged: root.loadProgress = web.loadingProgress
            }

            // Loading overlay (must not steal clicks once faded out)
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                color: Style.bgDeep
                opacity: web.loading ? 0.55 : 0
                visible: web.loading
                enabled: false
                Behavior on opacity { NumberAnimation { duration: 120 } }
                Column {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Loading..."
                        color: Style.textDim
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsBody
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: root.loadProgress > 0 && root.loadProgress < 100
                        text: root.loadProgress + "%"
                        color: Style.textFaint
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsTiny
                    }
                }
            }

            // Error / empty state
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                color: Style.bgDeep
                visible: (!web.loading && root.loadError.length > 0)
                         || (!web.loading && !root.hasLoadedOnce
                             && (!root.effectiveUrl || root.effectiveUrl.length === 0))
                Column {
                    anchors.centerIn: parent
                    width: parent.width - 32
                    spacing: Style.gapSm
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: root.loadError.length > 0
                              ? root.loadError
                              : "No URL — pass args.url (web) or args.videoId (YouTube)"
                        color: Style.textDim
                        font.family: Style.fontFamily
                        font.pixelSize: Style.fsSmall
                    }
                    PmButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: root.loadError.length > 0
                        text: "Retry"
                        onClicked: web.reload()
                    }
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
                text: "Reload"
                flat: true
                onClicked: {
                    root.loadError = ""
                    web.reload()
                }
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

    onEffectiveUrlChanged: {
        // When argsJson / videoId resolve after Loader sets properties, navigate.
        if (root.effectiveUrl && root.effectiveUrl.length > 0)
            Qt.callLater(root.applyEffectiveUrl)
    }

    Component.onCompleted: {
        if (args.mode) root.mode = args.mode
        if (args.url && (!root.url || root.url.length === 0))
            root.url = args.url
        if (args.videoId && root.videoId.length === 0)
            root.videoId = args.videoId

        // Script text is injected by main.cpp as a context property (Qt 6.5+
        // blocks QML XMLHttpRequest to qrc/local files by default).
        if (typeof ytCleanScriptText === "string" && ytCleanScriptText.length > 0) {
            root.ytCleanScript = ytCleanScriptText
            root.injectClean()
        }

        // Navigate after the item has a real size — WebEngineView often paints
        // blank if the first load happens at 0×0 inside a Loader.
        Qt.callLater(function() {
            if (webHost.width > 2 && webHost.height > 2)
                root.applyEffectiveUrl()
            else
                sizeKick.start()
        })
    }

    // Retry navigation once the host is laid out (SurfaceHost animates size).
    Timer {
        id: sizeKick
        interval: 50
        repeat: true
        property int tries: 0
        onTriggered: {
            tries++
            if ((webHost.width > 2 && webHost.height > 2) || tries > 40) {
                stop()
                root.applyEffectiveUrl()
            }
        }
    }
}
