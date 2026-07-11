# B3 — Adblock + clean-mode hardening: contract notes for B2

B3 does not touch `WebSurface.qml` (owned by B2). This documents the exact
qrc path of the new clean-mode script, the injection contract B2 must
implement to use it, and the extra-hosts file format for the interceptor.

## Files edited/created

- `src/ui/web_adblock_interceptor.h` / `.cpp` — extended default host list
  (2026 YouTube ad/analytics hardening), a wildcard-TLD host-prefix matcher
  (`adservice.google.*`), an optional `data/adblock_extra.txt` loader, and a
  new pure free function `polymath::isAdRequest(const QString& url)` used by
  the new unit test.
- `src/ui/qml/surfaces/YtClean.js` (new) — standalone page-context clean
  script, replaces the inline `ytCleanScript` string currently living in
  `WebSurface.qml` (~lines 31-60). **B2 should delete that inline string and
  inject this file's contents instead** (see contract below) — do not keep
  both; they'd double-install (harmless, since YtClean.js is idempotent, but
  redundant and a maintenance trap).
- `src/ui/CMakeLists.txt` — registers `qml/surfaces/YtClean.js` under
  `RESOURCES` (not `QML_FILES` — see rationale below).
- `tests/test_adblock.cpp` (new) + `tests/CMakeLists.txt` (append-only test
  registration, `adblock` ctest target) — unit tests for `isAdRequest()`.

## 1. `YtClean.js` — exact qrc path

```
qrc:/qt/qml/Polymath/qml/surfaces/YtClean.js
```

It is listed under `RESOURCES` (not `QML_FILES`) in `src/ui/CMakeLists.txt`,
matching how `fonts/Inter.ttf` / `icons/tray.png` are embedded — the qrc path
is `qrc:/qt/qml/Polymath/<path-as-listed>`, same convention as
`qrc:/qt/qml/Polymath/qml/Main.qml` (a `QML_FILES` entry) and
`qrc:/qt/qml/Polymath/fonts/Inter.ttf` (a `RESOURCES` entry) already in use
in `src/app/main.cpp`. **Deliberately not `QML_FILES`**: the file's content
is plain page-context JavaScript (a self-invoking function meant to run
inside the Chromium page via `runJavaScript()`), not a QML JS module — it is
never `import`ed by any `.qml` file, so it must not go through
qmlcachegen/qmllint's QML-JS-module compilation path (which expects either a
`.pragma library` module or QML-importable script semantics).

## 2. Injection contract B2 must implement

`YtClean.js`'s top-level code is a self-invoking `(function () { ... })()` —
evaluating its full text immediately installs the clean-mode behavior. No
exported symbol needs to be called separately. Replace
`WebSurface.qml`'s current `ytCleanScript` property + `injectClean()` body
with something equivalent to:

```qml
property string ytCleanScript: ""

Component.onCompleted: {
    var xhr = new XMLHttpRequest()
    xhr.open("GET", "qrc:/qt/qml/Polymath/qml/surfaces/YtClean.js")
    xhr.onreadystatechange = function() {
        if (xhr.readyState === XMLHttpRequest.DONE && xhr.status === 200)
            root.ytCleanScript = xhr.responseText
    }
    xhr.send()
}

function injectClean() {
    if ((root.isYouTube || root.resolvedMode === "video") && root.ytCleanScript.length > 0)
        web.runJavaScript(root.ytCleanScript)
}
```

Keep the existing call sites unchanged: `onLoadingChanged` (on
`LoadSucceededStatus`) and `onUrlChanged` via `Qt.callLater(root.injectClean)`
— both already call `root.injectClean()`, which is exactly where the fetched
script text should be run. `YtClean.js` is idempotent
(`window.__pmYtCleanInstalled` guard), so re-injection on every load/soft-nav
is safe and expected — it must NOT be gated to "first load only".

Works on both watch pages (`youtube.com/watch?v=...`) and `/embed/<id>`
pages — the DOM selectors used (`.ad-showing`, `.ytp-ad-*`,
`ytd-ad-slot-renderer`, etc.) and the generic `document.querySelector('video')`
mute logic are shared between both layouts, so no mode-specific branching is
needed in B2's injection code.

## 3. Clean script capabilities (`YtClean.js`)

- **CSS hide list** (2026 DOM): `ytd-ad-slot-renderer`,
  `ytd-in-feed-ad-layout-renderer`, `ytd-display-ad-renderer`,
  `ytd-promoted-sparkles-web-renderer`, `ytd-companion-slot-renderer`,
  `ytd-action-companion-ad-renderer`, `ytd-banner-promo-renderer`,
  `ytd-statement-banner-renderer`, `ytd-mealbar-promo-renderer`,
  `ytd-merch-shelf-renderer`, `#player-ads`, `#masthead-ad`, `.video-ads`,
  the full `.ytp-ad-*` pre/mid-roll overlay chrome, plus paid-promo /
  engagement-ad panels (`ytd-paid-content-overlay-renderer`,
  `.ytp-paid-content-overlay`, `ytd-engagement-panel-section-list-renderer
  [target-id*="ads"/"promo"]`).
- **Skip-button auto-click**, ~400ms interval:
  `.ytp-ad-skip-button`, `.ytp-ad-skip-button-modern`, `.ytp-skip-ad-button`,
  `.ytp-ad-skip-button-container button`, `.ytp-ad-overlay-close-button`.
- **Ad-showing mute**: mutes `document.querySelector('video')` while
  `.ad-showing`/`.ad-interrupting` is present on the player, restores the
  prior mute state once the ad clears (tracked via `video.__pmMutedForAd`,
  mirrors the old inline script's approach).
- **Idempotent**: `window.__pmYtCleanInstalled` guards the `setInterval` +
  initial `<style>` install; the style block itself is also re-appended if
  missing (belt-and-suspenders for a hard navigation on `/embed/` pages that
  clears `<head>`).
- **Out of scope** (Z-backlog per the DAG): SponsorBlock-style segment
  skipping. Not implemented, not stubbed.

## 4. Interceptor changes (`web_adblock_interceptor.h/.cpp`)

- New default hosts added: `googleads.g.doubleclick.net`,
  `imasdk.googleapis.com` (Google IMA SDK ad-request host),
  `2mdn.net` (DoubleClick creative CDN). (`doubleclick.net`,
  `googlesyndication.com`, `pagead2.googlesyndication.com`,
  `static.doubleclick.net`, `adservice.google.com` were already present —
  kept, and the DAG's explicit list is satisfied by the existing suffix
  match on `doubleclick.net`/`googlesyndication.com` for the `*`-prefixed
  entries.)
- New wildcard-TLD prefix matcher: `adservice.google.` — blocks
  `adservice.google.com`, `.de`, `.co.uk`, etc. (a plain suffix match on one
  hardcoded TLD would miss the others).
- `youtube.com/pagead/` and `youtube.com/ptracking` from the spec are already
  covered by the existing `/pagead/` and `/ptracking` path markers (checked
  against the full URL only when host contains `youtube.com`/
  `googlevideo.com`/`ytimg.com`) — no code change needed there.
- googlevideo `ctier=L` / `oad=` stream heuristics: unchanged, kept as-is.
- **`data/adblock_extra.txt`**: optional, loaded once at
  `WebAdblockInterceptor` construction from
  `Paths::instance().root() / "adblock_extra.txt"` (i.e. the deployed/portable
  `data/` folder beside the exe, or `%LOCALAPPDATA%/Polymath` when
  installed — `Paths::instance().setRoot()` runs in `main.cpp` before the
  interceptor is constructed, so the root is always resolved by then).
  Format: **one host (suffix-matched, same rule as the built-in list) per
  line**; blank lines ignored; lines starting with `#` are comments; hosts
  are lower-cased on load. Missing file is not an error (silently skipped —
  `extraHostsFileCount()` returns 0). Example:
  ```
  # personal extras
  some-tracker.example.com
  another-ad-host.net
  ```
- New pure function `polymath::isAdRequest(const QString& url)` (declared in
  `web_adblock_interceptor.h`) — classifies a URL against the **built-in
  default rules only** (does not see `addBlockedHost()` calls or the
  extra-hosts file, which are instance state). Used by
  `tests/test_adblock.cpp`; not needed by B2.

## 5. Test added

`tests/test_adblock.cpp` — links `pm_ui` (for `isAdRequest()`) + `Qt6::Core`,
registered as ctest target `adblock`. No network, no QApplication/QWebEngine
construction. Covers: doubleclick/googlesyndication/adservice (incl. two
TLDs)/imasdk hosts, YouTube `/pagead/`, `/ptracking`, `/api/stats/ads`,
`/youtubei/v1/log_event`, googlevideo `ctier=L` and `oad=` streams (positive
cases); a normal watch URL, a `youtube-nocookie.com` embed URL, a normal
googlevideo playback URL, `ytimg.com` thumbnail, unrelated `googleapis.com`
and `google.com` URLs (negative cases, i.e. must NOT be blocked).

## Manual verification note

Live "monetized watch page + embed page, ≥3/3 tries, no visible/audible
pre-roll" acceptance criterion from the DAG requires a running GPU/CPU build
with a real WebEngineView — out of scope for this node (no build/run
performed here per the STRICT RULES; orchestrator builds centrally). B2
should record that check in `results/B3_verify.md` (or its own results file)
once `WebSurface.qml` is wired to fetch-and-inject `YtClean.js` per the
contract above.
