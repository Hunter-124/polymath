# E3 — stub requirements for `capture_views.cpp` (EV applies)

E3 does not touch `src/ui/tools/capture_views.cpp` (EV-only, per the DAG global
rule). This documents what EV needs to add for clean PNGs of the new/changed
E3 surfaces, and a scripted `board` layout demo per E3's acceptance criterion
("spawn 2 notes + 2 images + 1 video in 2 groups → board layout renders
clean"). Builds on the pattern B2 already proposed in `results/B2_stubs.md`
(`View` struct extended with a per-entry seed snippet) — reuse that, don't
reinvent it.

## 1. New file needing CMake registration

`src/ui/qml/surfaces/NoteSurface.qml` is new and referenced from
`SurfaceHost.qml`'s `typeMap["note"]`. It must be added to `src/ui/CMakeLists.txt`
(orchestrator-owned, not edited here) alongside the other `qml/surfaces/*.qml`
entries or the QML module build will fail to resolve
`"surfaces/NoteSurface.qml"` at runtime.

## 2. `NoteSurface.qml` — standalone capture (optional but cheap)

Same shape as every other `qml/surfaces/*.qml` view: a `GlassCard` root with
plain string properties (`title`, `argsJson`, `md`, `group`) and no `app`/
context-property dependency. Using B2's proposed `extraOnLoaded` seed
mechanism:

```cpp
{"surfaces/NoteSurface.qml", "15-note-surface", false, kNoteSeedJs},
```

```qml
onStatusChanged: if (status === Loader.Ready) {
    item.title = "Castle research"
    item.md = "# Neuschwanstein\n\nBuilt for **Ludwig II** starting 1869; "
            + "never fully completed. Known for its fairy-tale silhouette "
            + "and heavy Wagner influence.\n\n- Book tickets online\n"
            + "- Bring good shoes\n- [Official site](https://example.org)"
}
```

Exercises the markdown rendering (heading/bold/list/link), the internal
`Flickable`/`PmScrollBar` scroll path, and the title bar in one shot.

## 3. `ImageSurface.qml` — standalone capture (optional but cheap)

Now that it has a caption bar and Fit/Fill toggle, a seed is worth adding:

```qml
onStatusChanged: if (status === Loader.Ready) {
    item.title = "Exterior"
    item.caption = "West façade, autumn 2019"
    // Leave item.source empty (see B2_stubs.md §2 reasoning: capture_views
    // runs fully offline/offscreen, so a remote URL just fails to load
    // silently and the capture becomes non-deterministic). Leaving it blank
    // exercises the real "No image source" placeholder path deterministically.
    // If EV wants a populated look, point at a bundled qrc: test image
    // instead of a remote URL.
}
```

This is enough to confirm the caption bar + Fit/Fill button render; the
click-to-focus `requestFocus` signal is a no-op in isolation (nothing
connects it outside `SurfaceHost`) so it's harmless to leave unexercised here.

## 4. `PlaceholderSurface.qml` — no seed needed

Renders a clean "Waiting for content…" state with zero properties set
(`title` defaults to `"Surface"`). Cheap to add as a bare entry
(`{"surfaces/PlaceholderSurface.qml", "16-placeholder-surface", false}`,
no seed snippet) if EV wants visual confirmation the argsJson dump is
actually gone; not required.

## 5. `SurfaceHost.qml` board layout — scripted multi-surface demo (the important one)

Per E3's acceptance criterion, this is the capture that actually matters.
`SurfaceHost` itself still has no seed-data path of its own by design (it's a
live overlay populated by `app.onSurfaceRequested`, same reasoning
`B2_stubs.md` §4 already gave) — the recommended approach is a small demo
wrapper view, exactly as B2_stubs.md §4 anticipated ("a `SurfaceHost`-level
demo wrapper... E3's board layout will want this more than B2 does").

**Recommended wrapper** (new small QML string or `qrc:` file, EV's call same
as B2's `extraOnLoaded` mechanism — does *not* need a `StubApp` signal
change, because it calls `SurfaceHost.spawn()` directly instead of going
through `app.onSurfaceRequested`):

```qml
import QtQuick
import Polymath
Window {
    width: 1280; height: 820; visible: true; color: Style.bg
    Component.onCompleted: Style.effectsEnabled = pmEffectsEnabled
    SurfaceHost {
        id: host
        anchors.fill: parent
        Component.onCompleted: {
            // Group "Castles" — 1 note + 1 image
            host.spawn("n1", "note", "Castle research",
                JSON.stringify({}), "",
                "# Neuschwanstein\n\nBuilt for **Ludwig II** starting 1869.\n\n"
                + "- Fairy-tale silhouette\n- Heavy Wagner influence",
                -1, -1, -1, -1, "Castles")
            host.spawn("i1", "image", "Exterior",
                JSON.stringify({}), "West façade, autumn 2019", "",
                -1, -1, -1, -1, "Castles")
            // Group "Trip planning" — 1 note + 1 image + 1 media card
            host.spawn("n2", "note", "Trip notes",
                JSON.stringify({}), "",
                "- Book tickets online\n- Bring good shoes\n- Check opening hours",
                -1, -1, -1, -1, "Trip planning")
            host.spawn("i2", "image", "Approach map",
                JSON.stringify({}), "Approach trail from the car park", "",
                -1, -1, -1, -1, "Trip planning")
            host.spawn("v1", "video_picker", "Search: castle drone footage",
                JSON.stringify({results: [
                    {videoId:"aBcDeFgHiJk", title:"Neuschwanstein Castle drone footage 4K",
                     channel:"Skyline Drones", durationSec:312, views:"845K views",
                     publishedText:"1 year ago", thumbnailUrl:"", watchUrl:""}
                ]}), "", "", -1, -1, -1, -1, "Trip planning")
            host.arrange("board")
        }
    }
}
```

**Why `video_picker` instead of a literal `"video"` (WebSurface) surface for
the 5th card:** `B2_stubs.md` §3 already established that `WebSurface.qml`
(real `QtWebEngine`) must NOT go in the headless `capture_views` harness —
no Chromium subprocess init in `capture_views`'s `main()`, and
`QT_QPA_PLATFORM=offscreen` + WebEngine is flaky/hang-prone. `video_picker`
(`VideoPickerSurface.qml`, already proven capture-safe by B2) is the
board's stand-in "media card" type here so the *whole* board demo stays
headless-safe. The literal owner acceptance line ("2 notes + 2 images + 1
video") is satisfied in spirit — a media/video-shaped card sits in the
board — with the actual playing-embed case remaining a manual/live check,
same split B2 already made. If EV wants a literal `type:"video"` card in the
screenshot anyway, it needs the WebEngine-init workaround called out in
`B2_stubs.md` §3, not something to improvise here.

This demo exercises: group frames + labels (2 distinct `args.group` values
rendered as bordered/labeled panels), notes-column-beside-media placement
within each group, the toolbar's new "Board" button (visible once `surfaces`
is non-empty), and `NoteSurface`'s markdown rendering inside the board
context. Auto-placement only (no surface in the seed sets x/y/w/h hints) —
if EV also wants to prove the A3 hint path, add `w`/`h` (and `x`/`y` >= 0) to
one or two of the seed calls and confirm they land at the explicit offset
instead of the auto-flow position.

## 6. `StubApp::surfaceRequested` — optional, not required by this demo

`A3_notes.md` already flagged that `StubApp`'s stub `surfaceRequested`
signal (`capture_views.cpp:188`) only has the original 5-param signature
(`id, action, type, title, argsJson`), and suggested EV extend it to the full
12-param form if a capture view needs to assert on caption/md/x/y/w/h/group
*coming through the `app` signal path*. The board demo above sidesteps that
entirely by calling `SurfaceHost.spawn()` directly, so no `StubApp` change is
required for E3's own acceptance bar — only relevant if EV separately wants
to test the `AppController → app.surfaceRequested → SurfaceHost.spawn()`
relay path itself under capture (e.g. for a future E4 handler).
