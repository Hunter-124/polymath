# E4 — Window takeover (QML side of A3)

**File edited:** `src/ui/qml/Main.qml` only. `src/app/app_controller.cpp/.h` was
**not touched** — A3 already added the `navigateRequested(QString page)` and
`windowRequested(QString verb)` signals (confirmed in `app_controller.h:195-197`)
and wires them 1:1 off the bus in `wireEventBus()`, so everything needed for
this node is expressible in QML with `ApplicationWindow`'s existing
`showFullScreen()/showNormal()/showMaximized()/raise()/requestActivate()/
flags/visibility` surface. No new Q_INVOKABLE or window-handle exposure was
required.

## 1. Navigation mechanism reused

`onNavigateRequested(page)` calls a new `window.goToPageId(id)` helper
(`Main.qml:247-251`), which looks the canonical id up in a new
`readonly property var pageIdMap` (`Main.qml:117-130`) — a literal copy of
A3's fixed id→display-name contract from `docs/overhaul2/results/A3_notes.md`
— and then calls the **existing** `window.goToPage(name)` function
(unchanged, `Main.qml:243-246`), which is the same function every nav-rail
button (`onClicked: window.currentPage = modelData.pageIndex` uses the index
directly, but `goToPage` is the name-based entry point already used by
`paletteActions`, `openSettings`, `NotificationCenter.onNavigateRequest`, and
the legacy `open_page`-via-`SurfaceRequest` handler) and the command palette
already call. Unknown ids fail the `pageIdMap` lookup silently (`goToPageId`
no-ops) rather than crashing — matches A3's note that unresolved page strings
should "log/ignore rather than crash."

I left the pre-existing `Connections { function onSurfaceRequested(...) }`
open_page handler (`Main.qml:1046-1063`) in place rather than deleting it —
A3's notes call that path "dead in practice" now that `open_page` publishes
`NavigateRequest` instead, but it's harmless (only fires when
`action==="open_page"`, which no producer emits anymore per A3) and removing
it wasn't in E4's file-ownership scope beyond "Connections + handlers."

## 2. Window-verb handler behavior (`Main.qml:294-394`, dispatched via a new
`Connections` block at `Main.qml:978-988`)

State tracked: `aiTakeoverActive` (true whenever the AI holds fullscreen,
always-on-top, or present), `aiOnTop` (AI-forced `WindowStaysOnTopHint`
currently applied), `priorWasMaximized` (captured once per takeover session
so exit restores the right shape).

| verb | behavior |
|---|---|
| `present` | `aiBeginTakeover()` (remembers prior maximize state, arms the auto-revert timer) → `raise()` + `requestActivate()` → `showFullScreen()` if not already fullscreen. The SurfaceHost overlay needs no extra call to "show" — it's `anchors.fill: parent, z: 3`, always on top of the page stack by construction, so raising+fullscreening the window is sufficient to put spawned surfaces in front of the user. |
| `fullscreen` | `aiBeginTakeover()` → `showFullScreen()` if not already fullscreen. |
| `restore` | Stops the revert timer; if currently fullscreen, `showMaximized()`/`showNormal()` per `priorWasMaximized`; clears `aiTakeoverActive` unless `aiOnTop` is still held (so `restore` after `present` fully exits, but `restore` while `always_on_top` is separately active doesn't drop the pill/on-top state). |
| `always_on_top` | `aiBeginTakeover()` → sets `aiOnTop=true`, ORs `Qt.WindowStaysOnTopHint` into `flags`, then `hide(); show()` — the visibility juggle Qt requires for a flags change to actually take effect on Windows. |
| `normal` | Clears `aiOnTop`, ANDs `~Qt.WindowStaysOnTopHint` out of `flags`, `hide(); show()`; clears `aiTakeoverActive` unless still fullscreen. |
| `raise` | `raise()` + `requestActivate()` only — no state change, doesn't touch fullscreen/on-top/`aiTakeoverActive`, matches the spec's "bring to front without stealing full-screen state." |
| `hide_to_tray` | `window.hide()` directly — the exact same call `onClosing`'s tray-intercept path uses (`Main.qml:61-66`), so it's indistinguishable from the user clicking the titlebar close button; the tray icon's "Show Polymath" / left-click still restores via the existing `restoreWindow()`. |
| unknown verb | falls to `default:` → `console.log(...)`, no-op, no crash. |

## 3. Human override — Esc / pill / auto-revert (`Main.qml:959-1044`)

- **Esc**: a new `Shortcut { sequence: "Esc"; context: Qt.ApplicationShortcut;
  enabled: window.aiTakeoverActive; onActivated: window.aiExitTakeover() }`.
  Scoped to `enabled: aiTakeoverActive` specifically so it's inert (and can't
  compete with `CommandPalette`'s own internal Escape-to-close key handling)
  except during an actual AI takeover.
- **Pill**: a `Rectangle` (`id: presentPill`, `Main.qml:993-1044`) anchored
  top-center, `z: 6` (above titlebar `z:2`, page host `z:1`, and the `z:3`
  overlay layer, so it stays visible even in fullscreen/present mode over a
  spawned video surface), `visible: window.aiTakeoverActive`, glass-card style
  built from `Style.surface3`/`Style.border`/`Style.radiusPill`/
  `Style.shadowA1` (matches `ToastStack`'s card treatment, chosen over the
  titlebar's translucent `palettePill` look because the pill needs to read
  clearly over arbitrary fullscreen content like a playing video, not just
  the aurora background). Text: *"Polymath is presenting — Esc to dismiss"*.
  Clicking the pill also calls `aiExitTakeover()` as a mouse-accessible
  equivalent to pressing Esc.
- **Auto-revert timeout**: a `Timer { id: presentTimeoutTimer }`
  (`Main.qml:971-976`) armed by `aiBeginTakeover()` for
  `presentTimeoutMin * 60000` ms, single-shot, calling `aiExitTakeover()` on
  fire. `presentTimeoutMin` is read defensively in `aiBeginTakeover()`
  (`Main.qml:307-313`):
  ```qml
  var mins = 30
  if (typeof settings !== "undefined" && settings
          && typeof settings.getInt === "function") {
      var v = settings.getInt("ui.present_timeout_min", 30)
      if (v > 0) mins = v
  }
  ```
  Verified this is safe today with no config-key edit: the real
  `SettingsController::getInt` (`src/ui/settings_controller.cpp:162-164`)
  forwards straight to `Config::getInt(key, def)`, which returns `def` when
  the key is unset — so this reads `30` right now and will pick up whatever
  A4/config seeds for `ui.present_timeout_min` later with zero further QML
  changes. The headless-capture `StubSettings::getInt` (`capture_views.cpp:218`)
  also always returns `def`, so capture never depends on this key existing
  either.
- `aiExitTakeover()` (`Main.qml:322-337`) is the single revert path used by
  Esc, the pill click, and the timeout: drops `WindowStaysOnTopHint` (with the
  hide/show juggle) if `aiOnTop`, restores `showMaximized()`/`showNormal()`
  per `priorWasMaximized` if currently fullscreen, clears
  `aiTakeoverActive`, then `raise()` + `requestActivate()` so focus lands back
  on the now-normal window.

## 4. Config key for the orchestrator to seed

**`ui.present_timeout_min`** (int, default **30**) — not added here per the
STRICT RULES (A4 owns `config.h`/`.cpp` `keys` namespace + `seedDefaults()`
this batch). E4 reads it defensively via `settings.getInt(...)` with a
hardcoded `30` fallback, so functionality is correct even if the key is never
seeded; seeding it just makes it user-configurable from Settings later
(no Settings UI was added for it in this node — out of scope, `SettingsView.qml`
isn't owned by E4).

## 5. `capture_views` / EV concern

`src/ui/tools/capture_views.cpp`'s `StubApp` (confirmed by reading it,
`capture_views.cpp:111-192`) does **not** declare `navigateRequested` or
`windowRequested` signals — exactly as A3's notes flagged. The new
`Connections { target: app; ignoreUnknownSignals: true; function
onNavigateRequested(...) {...} function onWindowRequested(...) {...} }` block
uses `ignoreUnknownSignals: true` (same pattern as the pre-existing
`onSurfaceRequested` Connections block right below it), so headless capture
against the current `StubApp` will **not crash** — the handlers simply never
fire, and `capture_views` should stay green as-is. **For EV to add** (per
A3's stub-sync section, so a future capture run can actually exercise this
code path and screenshot the pill/present state):
```cpp
signals:
    ...
    void navigateRequested(QString);
    void windowRequested(QString);
```
No `StubSettings` change is required — `getInt(key, def)` already returns
`def` unconditionally regardless of key, which is exactly the "no config key
seeded yet" behavior this node depends on.

## Confirmation

- Only `src/ui/qml/Main.qml` edited. `app_controller.cpp/.h` untouched — A3's
  signals were sufficient.
- No new imports (`QtQuick`, `QtQuick.Controls.Basic`, `QtQuick.Layouts`,
  `Qt.labs.platform`, `Polymath` — unchanged).
- All existing titlebar/move/resize/maximize/tray behavior is untouched:
  `restoreWindow()`, `onClosing`, `toggleMaximize()`, the `ChromeBtn`s, the
  `DragHandler`/`TapHandler` on the titlebar, and all eight `ResizeEdge`
  instances are byte-for-byte as before (verified by reading the full file
  after editing — see `Main.qml:49-91`, `260-292` region only had the new
  E4 block appended after `toggleMaximize()`, and `729-1110` region only has
  the new Shortcut/Timer/Connections/pill inserted between the pre-existing
  `Ctrl+K` Shortcut and the pre-existing open_page Connections block).
- Colors/sizes are 100% `Style.*` tokens (`Style.surface3`, `Style.border`,
  `Style.radiusPill`, `Style.shadowA1`, `Style.gapLg`, `Style.gapSm`,
  `Style.controlH`, `Style.accent`, `Style.text`, `Style.fontFamily`,
  `Style.fsSmall`, `Style.durBase`) — no hardcoded hex/px values introduced.
