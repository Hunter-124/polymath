// YtClean.js — Polymath YouTube clean-mode (B3).
//
// This is PAGE-CONTEXT JavaScript, not a QML JS module. It is meant to be
// read as raw text (e.g. via an XMLHttpRequest/fetch against its qrc: path,
// or a C++ QFile read of the deployed resource) and injected into a
// WebEngineView with `runJavaScript(text)` after a YouTube watch or /embed/
// page finishes loading. See docs/overhaul2/results/B3_contract.md for the
// exact injection contract B2 (WebSurface.qml) implements against this file.
//
// Responsibilities:
//   1. Inject CSS that hides 2026-era YouTube ad DOM (pre-roll/mid-roll
//      overlays, in-feed/masthead/display ad slots, paid-promo panels).
//   2. Auto-click "Skip Ad" buttons on a ~400ms interval.
//   3. Mute the <video> element while an ad is showing; unmute afterwards.
//   4. Work on both /watch pages and /embed/<id> pages.
//   5. SponsorBlock segment skip (Wave Z): fetch skipSegments and seek past
//      sponsor/intro/selfpromo/interaction when video.sponsorblock is on
//      (default). Set window.__pmSponsorBlock = false before inject to disable.
//   6. Be idempotent — safe to inject more than once (double-injection is
//      expected: onLoadingChanged fires per navigation, and YouTube's watch
//      page is a SPA that soft-navigates without a full reload).
//
// Contract: this file's top-level code runs immediately when evaluated (it
// is a self-invoking function), so simply injecting its full text is enough
// — no exported symbol needs to be called separately.

(function () {
  'use strict';

  // --- Idempotency guard -----------------------------------------------
  // Re-injection (soft nav, reload of onLoadingChanged) must not stack a
  // second setInterval or append a second <style> block.
  if (window.__pmYtCleanInstalled) return;
  window.__pmYtCleanInstalled = true;

  // SponsorBlock: default on unless host sets window.__pmSponsorBlock = false.
  var sbEnabled = (window.__pmSponsorBlock !== false);
  var sbSegments = [];   // [{start, end, category}, ...]
  var sbVideoId = '';
  var sbFetchInFlight = false;

  function parseVideoId() {
    try {
      var u = new URL(location.href);
      if (u.hostname.indexOf('youtu.be') >= 0) {
        return (u.pathname || '').replace(/^\//, '').split('/')[0] || '';
      }
      var v = u.searchParams.get('v');
      if (v) return v;
      // /embed/VIDEOID or /shorts/VIDEOID
      var m = (u.pathname || '').match(/\/(?:embed|shorts)\/([A-Za-z0-9_-]{6,})/);
      return m ? m[1] : '';
    } catch (e) { return ''; }
  }

  function fetchSponsorSegments(vid) {
    if (!sbEnabled || !vid || sbFetchInFlight) return;
    if (vid === sbVideoId && sbSegments.length) return;
    sbFetchInFlight = true;
    sbVideoId = vid;
    sbSegments = [];
    var cats = ['sponsor', 'intro', 'selfpromo', 'interaction', 'outro', 'preview', 'music_offtopic'];
    var url = 'https://sponsor.ajay.app/api/skipSegments?videoID=' +
              encodeURIComponent(vid) + '&categories=' +
              encodeURIComponent(JSON.stringify(cats));
    fetch(url, { method: 'GET', mode: 'cors', credentials: 'omit' })
      .then(function (r) { return r.ok ? r.json() : []; })
      .then(function (data) {
        sbFetchInFlight = false;
        if (!Array.isArray(data)) return;
        sbSegments = data.map(function (s) {
          var seg = s.segment || [];
          return {
            start: Number(seg[0]) || 0,
            end: Number(seg[1]) || 0,
            category: s.category || 'sponsor'
          };
        }).filter(function (s) { return s.end > s.start; });
      })
      .catch(function () { sbFetchInFlight = false; /* offline / CORS: ignore */ });
  }

  function skipSponsorSegments(video) {
    if (!sbEnabled || !video || !sbSegments.length) return;
    if (video.paused && !video.seeking) return; // only while playing-ish
    var t = video.currentTime;
    if (!isFinite(t)) return;
    for (var i = 0; i < sbSegments.length; i++) {
      var s = sbSegments[i];
      // Enter segment (with small lead-in) → seek to end + 0.05s
      if (t >= s.start && t < s.end - 0.05) {
        try { video.currentTime = s.end + 0.05; } catch (e) {}
        break;
      }
    }
  }

  // --- 1. CSS: hide 2026 YouTube ad DOM ---------------------------------
  var AD_HIDE_CSS = [
    // Pre-roll / mid-roll player overlay chrome.
    '.ytp-ad-module,',
    '.ytp-ad-overlay-container,',
    '.ytp-ad-image-overlay,',
    '.ytp-ad-player-overlay,',
    '.ytp-ad-player-overlay-instream-info,',
    '.ytp-ad-text-overlay,',
    '.ytp-ad-progress-list,',
    '.ytp-ad-preview-container,',
    '.ytp-ad-persistent-progress-bar-container,',
    // In-feed / watch-page ad slot renderers (2026 element names).
    'ytd-ad-slot-renderer,',
    'ytd-in-feed-ad-layout-renderer,',
    'ytd-display-ad-renderer,',
    'ytd-promoted-sparkles-web-renderer,',
    'ytd-promoted-video-renderer,',
    'ytd-companion-slot-renderer,',
    'ytd-action-companion-ad-renderer,',
    'ytd-banner-promo-renderer,',
    'ytd-statement-banner-renderer,',
    'ytd-mealbar-promo-renderer,',
    'ytd-merch-shelf-renderer,',
    '#player-ads,',
    '#masthead-ad,',
    '.video-ads,',
    // Paid-promotion / engagement panels.
    'ytd-paid-content-overlay-renderer,',
    '.ytp-paid-content-overlay,',
    'ytd-engagement-panel-section-list-renderer[target-id*="ads"],',
    'ytd-engagement-panel-section-list-renderer[target-id*="promo"]',
    '{ display:none !important; visibility:hidden !important;',
    '  height:0 !important; min-height:0 !important; }',
    // Ad-showing overlays on the player itself (do not hide the <video>).
    '.ad-showing .ytp-ad-skip-button-container,',
    '.ad-interrupting .ytp-ad-skip-button-container',
    '{ pointer-events:auto; }'
  ].join('');

  function ensureStyleInjected() {
    if (document.getElementById('__pmYtCleanStyle')) return;
    var css = document.createElement('style');
    css.id = '__pmYtCleanStyle';
    css.textContent = AD_HIDE_CSS;
    (document.head || document.documentElement).appendChild(css);
  }
  ensureStyleInjected();

  // --- 2 & 3. Skip-button auto-click + ad-showing mute, on an interval --
  var SKIP_SELECTORS = [
    '.ytp-ad-skip-button',
    '.ytp-ad-skip-button-modern',
    '.ytp-skip-ad-button',
    '.ytp-ad-skip-button-container button',
    'button.ytp-ad-skip-button-modern',
    '.ytp-ad-overlay-close-button'
  ];

  function clickSkipButtons() {
    for (var i = 0; i < SKIP_SELECTORS.length; i++) {
      var b = document.querySelector(SKIP_SELECTORS[i]);
      if (b) {
        try { b.click(); } catch (e) { /* button not interactable yet */ }
      }
    }
  }

  function isAdShowing() {
    // Covers both the watch-page player (#movie_player.ad-showing) and the
    // /embed/ player (.html5-video-player.ad-showing) — the class is applied
    // to the player container in both layouts.
    return !!(document.querySelector('.ad-showing') ||
              document.querySelector('.ad-interrupting'));
  }

  function tick() {
    // Re-append the style block if the page nuked <head> contents on a hard
    // navigation (soft SPA nav keeps it; belt-and-suspenders for /embed/).
    ensureStyleInjected();
    clickSkipButtons();

    var ad = isAdShowing();
    var v = document.querySelector('video');
    if (!v) return;
    if (ad && !v.muted) {
      v.muted = true;
      v.__pmMutedForAd = true;
    } else if (!ad && v.__pmMutedForAd) {
      v.muted = false;
      v.__pmMutedForAd = false;
    }

    // SponsorBlock: refresh segments on soft-nav video change; skip while playing.
    var vid = parseVideoId();
    if (vid && vid !== sbVideoId) fetchSponsorSegments(vid);
    else if (vid && !sbSegments.length && !sbFetchInFlight) fetchSponsorSegments(vid);
    if (!ad) skipSponsorSegments(v);
  }

  // ~400ms per the B3 spec — frequent enough to catch short skip-button
  // windows without noticeably taxing the page.
  window.__pmYtCleanInterval = window.setInterval(tick, 400);
  tick();
})();
