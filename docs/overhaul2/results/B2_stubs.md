# B2 — stub requirements for `capture_views.cpp` (EV applies)

B2 does not touch `src/ui/tools/capture_views.cpp` (EV-only, per the DAG global
rule). This documents exactly what EV needs to add to get a clean PNG of
`VideoPickerSurface.qml` with ~6 fake results, plus a note on why
`WebSurface.qml` should NOT be added to the capture harness.

## 1. `VideoPickerSurface.qml` — new `views[]` entry

`VideoPickerSurface.qml` (`src/ui/qml/surfaces/VideoPickerSurface.qml`) needs
**no** `app` context property and **no** stub list model — its only external
inputs are the `title` and `argsJson` string properties (same shape every
other surface under `qml/surfaces/` uses: `GlassCard` root, `argsJson` parsed
internally via `JSON.parse`). That means it can go through the *same* generic
wrapper `renderView()` already uses for every other view (the
`ApplicationWindow { Loader { source: <url> } }` wrapper string built around
line ~460-483 of `capture_views.cpp`) — it just needs the wrapper's Loader to
set `argsJson` after load, which none of the current `views[]` entries need
because none of them take constructor-style args today.

**Minimal approach (recommended):** add a second small wrapper string (or a
`propsJs` parameter threaded through the existing `renderView` lambda) that,
after `Loader.onLoaded`, does:

```qml
onStatusChanged: if (status === Loader.Ready) {
    item.title = "Watch: castles"
    item.argsJson = JSON.stringify({ results: <SEED_ARRAY_BELOW> })
}
```

i.e. extend `renderView`'s wrapper template with an optional `extraOnLoaded`
JS snippet parameter (empty string for all existing 13 views — zero behavior
change for them), and add:

```cpp
struct View { const char* file; const char* png; bool window; const char* extraOnLoaded = ""; };
...
{"surfaces/VideoPickerSurface.qml", "14-video-picker", false, kVideoPickerSeedJs},
```

where `kVideoPickerSeedJs` is a `static const char*` holding the
`item.title = ...; item.argsJson = ...;` snippet with the seed array
inlined (see §2). Alternatively, a standalone `qrc:/wrapper-video-picker.qml`
built the same way `comp.setData(wrapper.toUtf8(), ...)` already builds the
generic wrapper — whichever is less invasive to the existing `renderView`
signature is fine; both are EV's call.

No new context property, no `StubApp` change, no `StubListModel` needed —
this is the simplest of the wave-B/E surface captures.

## 2. Seed data — 6 fake results (youtube_search shape)

Matches the exact fields `VideoPickerSurface.qml` reads
(`videoId, title, channel, durationSec, views, publishedText, thumbnailUrl,
watchUrl`) and the shape B1's `youtube_search` tool returns:

```json
[
  { "videoId": "dQw4w9WgXcQ", "title": "Castles of Scotland — a walking tour through 900 years of history",
    "channel": "Heritage Explorer", "durationSec": 754, "views": "1.2M views",
    "publishedText": "3 years ago", "thumbnailUrl": "", "watchUrl": "https://www.youtube.com/watch?v=dQw4w9WgXcQ" },
  { "videoId": "aBcDeFgHiJk", "title": "Neuschwanstein Castle drone footage 4K",
    "channel": "Skyline Drones", "durationSec": 312, "views": "845K views",
    "publishedText": "1 year ago", "thumbnailUrl": "", "watchUrl": "https://www.youtube.com/watch?v=aBcDeFgHiJk" },
  { "videoId": "kLmNoPqRsTu", "title": "How medieval castles actually defended against siege engines",
    "channel": "History Lab", "durationSec": 1123, "views": "3.4M views",
    "publishedText": "8 months ago", "thumbnailUrl": "", "watchUrl": "https://www.youtube.com/watch?v=kLmNoPqRsTu" },
  { "videoId": "vWxYzAbCdEf", "title": "Edinburgh Castle full tour (no talking, ambient only)",
    "channel": "Wander Quiet", "durationSec": 2640, "views": "210K views",
    "publishedText": "2 weeks ago", "thumbnailUrl": "", "watchUrl": "https://www.youtube.com/watch?v=vWxYzAbCdEf" },
  { "videoId": "gHiJkLmNoPq", "title": "Top 10 fairytale castles in Europe you can actually visit",
    "channel": "Offbeat Travel", "durationSec": 611, "views": "5.7M views",
    "publishedText": "5 years ago", "thumbnailUrl": "", "watchUrl": "https://www.youtube.com/watch?v=gHiJkLmNoPq" },
  { "videoId": "rStUvWxYzAb", "title": "Building a castle with hand tools only — 3 year timelapse",
    "channel": "Guédelon Project", "durationSec": 5431, "views": "18M views",
    "publishedText": "10 months ago", "thumbnailUrl": "", "watchUrl": "https://www.youtube.com/watch?v=rStUvWxYzAb" }
]
```

`thumbnailUrl` is deliberately left `""` in the seed: `capture_views` runs
fully offline/offscreen (`QT_QPA_PLATFORM=offscreen`, no network), so a real
remote thumbnail URL would just fail to load silently and the card would
render however `Image` renders a failed-async-load in the offscreen backend
(usually blank, not necessarily the card's own "No thumbnail" placeholder,
since that placeholder is keyed off `!thumbnailUrl` being falsy, not off load
success/failure). Leaving it blank exercises the real, deterministic
"No thumbnail" placeholder path and keeps the capture stable/reproducible.
If EV wants a populated-thumbnail look for the screenshot, point
`thumbnailUrl` at a bundled `qrc:` test image instead of a remote URL (a tiny
placeholder JPG under `src/ui/icons/` registered as a `RESOURCES` entry would
work) — not required for the "looks clean" acceptance bar.

## 3. `WebSurface.qml` — do NOT add to the capture harness

Recommend EV skip adding `WebSurface.qml` (and `type:"video"`) to `views[]`.
It embeds a real `QtWebEngine` `WebEngineView`, which needs the Chromium
subprocess machinery (`QtWebEngineProcess`, GPU/software compositing paths)
initialized in `main()` — `capture_views` deliberately does not link `pm_app`
and has none of that init, and `QT_QPA_PLATFORM=offscreen` combined with
WebEngine has historically been flaky/hang-prone across Qt versions on
Windows. The B2 DAG acceptance criterion is explicit that the *manual*
"clicking a card swaps to playing embed" check and the ad-free-playback check
are live/manual verification, not `capture_views` PNG checks — only the
`VideoPickerSurface` PNG is in-scope for headless capture. If EV wants a
`WebSurface` screenshot anyway, treat it as a stretch item and expect to
special-case the WebEngine profile/process init or accept a real (non-CI)
GPU/CPU build run instead of the offscreen harness.

## 4. `SurfaceHost.qml` itself — not added either

Same reasoning as `PlaceholderSurface`/`ImageSurface` today (neither is in
`views[]`): `SurfaceHost` binds to `app.onSurfaceRequested` via `Connections`
and only renders content once `surfaces` is populated by that signal — it has
no seed-data path of its own, by design (it's a live overlay, not a page).
Capturing the surfaces directly (as this document does for
`VideoPickerSurface`) is the established pattern for surface PNGs; a
`SurfaceHost`-level "demo wrapper" that pre-populates `surfaces` via
`Component.onCompleted: root.spawn(...)` would be a reasonable *addition* if
EV wants a multi-surface board screenshot (E3's board layout will want this
more than B2 does) — not required for B2's own acceptance bar.
