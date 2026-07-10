import QtQuick
import QtQuick.Layouts
import QtWebEngine
import Polymath

// WebSurface — real QtWebEngine embed (D5): shared adblock profile + YouTube clean mode.
GlassCard {
    id: root
    property string title: "Web"
    property string url: ""
    property string argsJson: ""
    property string mode: "page"   // page | video (YouTube clean)

    section: "Chat"
    implicitWidth: 480
    implicitHeight: 300
    clip: true

    readonly property var args: {
        if (!argsJson || argsJson.length === 0) return ({})
        try { return JSON.parse(argsJson) } catch (e) { return ({}) }
    }
    readonly property string resolvedUrl: (args && args.url) ? args.url : root.url
    readonly property string resolvedMode: (args && args.mode) ? args.mode : root.mode
    readonly property bool isYouTube: {
        var u = (resolvedUrl || "").toLowerCase()
        return u.indexOf("youtube.com") >= 0 || u.indexOf("youtu.be") >= 0
    }

    // YouTube clean-mode: hide ad chrome, auto-click skip, mute mid-rolls.
    readonly property string ytCleanScript: "
(function() {
  if (window.__pmYtClean) return;
  window.__pmYtClean = true;
  var css = document.createElement('style');
  css.textContent = [
    '.ytp-ad-module,.ytp-ad-overlay-container,.ytp-ad-image-overlay,',
    'ytd-ad-slot-renderer,#player-ads,.video-ads,.ytp-ad-player-overlay,',
    'ytd-banner-promo-renderer,.ytd-mealbar-promo-renderer,',
    '#masthead-ad,.ytp-ad-text-overlay,.ytp-ad-progress-list',
    '{ display:none !important; visibility:hidden !important; height:0 !important; }'
  ].join('');
  (document.head || document.documentElement).appendChild(css);
  setInterval(function() {
    var sel = [
      '.ytp-ad-skip-button','.ytp-ad-skip-button-modern',
      '.ytp-skip-ad-button','.ytp-ad-skip-button-container button',
      'button.ytp-ad-skip-button-modern'
    ];
    for (var i = 0; i < sel.length; i++) {
      var b = document.querySelector(sel[i]);
      if (b) { try { b.click(); } catch (e) {} }
    }
    var ad = document.querySelector('.ad-showing');
    var v = document.querySelector('video');
    if (ad && v && !v.muted) { v.muted = true; v.__pmMutedForAd = true; }
    else if (!ad && v && v.__pmMutedForAd) { v.muted = false; v.__pmMutedForAd = false; }
  }, 400);
})();
"

    function injectClean() {
        if (root.isYouTube || root.resolvedMode === "video")
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
                text: root.title
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
                url: root.resolvedUrl.length > 0 ? root.resolvedUrl : "about:blank"
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
    }

    Component.onCompleted: {
        if (args.mode) root.mode = args.mode
        if (args.url && (!root.url || root.url.length === 0))
            root.url = args.url
    }
}
