# A3 — ui_control schema v2: contract notes for E4 / E3 / B2

Node A3 is schema/relay only (no QML edits). This documents the exact
EventBus payloads and AppController signals added/changed, and the fixed
page-id enum, so downstream QML nodes (E4 window takeover + open_page
handlers, E3 research-board surfaces, B2 video surface) can consume them
without re-reading the C++.

## Files edited

- `src/agent/tools/uicontrol_tool.h` / `.cpp` — new `window` action, `open_page`
  now publishes `NavigateRequest` instead of a no-op `SurfaceRequest`,
  `spawn_surface` args extended, tool description rewritten with tight enums +
  4 few-shot examples.
- `src/core/event_bus.h` / `.cpp` — `SurfaceRequest` extended with optional
  fields; two new payload structs (`NavigateRequest`, `WindowRequest`) +
  signals + `qRegisterMetaType`/`Q_DECLARE_METATYPE`.
- `src/app/app_controller.h` / `.cpp` — relay wiring only: `surfaceRequested`
  QML signal extended with the new trailing params, two new QML signals
  (`navigateRequested`, `windowRequested`).

## New/changed EventBus payloads (`src/core/event_bus.h`)

### `SurfaceRequest` (extended, backward compatible)

```cpp
struct SurfaceRequest {
    QString id;
    QString action;      // spawn|close|arrange|open_page (open_page path is now
                          // dead in practice — open_page publishes NavigateRequest
                          // instead; the action string is kept in the enum only
                          // for historical/back-compat readers)
    QString type;
    QString title;
    QString args_json;
    // A3 additions — all optional, default "empty":
    QString caption;      // "" if unset
    QString md;           // "" if unset — markdown body for a future text/note card
    double  x = -1;        // -1 = unset (no position hint given)
    double  y = -1;
    double  w = -1;
    double  h = -1;
    QString group;        // "" if unset — research-board grouping key
};
```

Producers that only set the original 5 fields are unaffected — new fields
default to blank/-1.

### `NavigateRequest` (new)

```cpp
struct NavigateRequest {
    QString page;       // canonical page id, see enum below
    QString args_json;  // reserved passthrough, usually "{}"
};
```

Published by `ui_control {action:"open_page", page:...}`. Bus signal:
`EventBus::navigateRequested(const NavigateRequest&)`.

### `WindowRequest` (new)

```cpp
struct WindowRequest {
    QString verb;   // present|fullscreen|restore|always_on_top|normal|raise|hide_to_tray
};
```

Published by `ui_control {action:"window", verb:...}`. Bus signal:
`EventBus::windowRequested(const WindowRequest&)`.

## AppController QML-facing signals (`src/app/app_controller.h`)

```cpp
// Existing signal, extended (new params appended at the end — old QML
// Connections handlers with only 5 formal params keep working unchanged):
void surfaceRequested(QString id, QString action, QString type, QString title,
                       QString argsJson, QString caption, QString md,
                       double x, double y, double w, double h, QString group);

// New:
void navigateRequested(QString page);
void windowRequested(QString verb);
```

`AppController::wireEventBus()` relays 1:1 from the bus signals above — no
logic beyond struct-field flattening.

## Canonical page-id enum (fixed contract — E4 maps id → nav rail page name)

Lowercase snake_case ids, chosen to be reliable for a small local model. They
correspond 1:1 to `Main.qml`'s `pages[].name` (display names in parens):

```
dashboard      (Dashboard)
chat           (Chat)
cameras        (Cameras)
timeline       (Timeline)
tasks          (Tasks)
shopping       (Shopping)
agents         (Agents)
personalities  (Personalities)
models         (Models)
privacy        (Privacy)
mobile_access  (Mobile Access)
settings       (Settings)
```

`uicontrol_tool.cpp` normalizes the incoming `page` argument before
publishing: lowercases, maps spaces/dashes to underscores, then resolves
through an alias table (`normalizePageId` / `pageAliases()` in
`uicontrol_tool.cpp`) so both the id form (`"settings"`) and natural/display
forms (`"Settings"`, `"settings page"`, `"home"` → dashboard, `"mobile"` →
mobile_access) land on the same canonical id. Unknown strings pass through
unchanged (lowercased/underscored) so E4's QML mapping can log/ignore rather
than crash.

**E4 action item:** in the `Connections { target: app }` handler for
`onNavigateRequested(page)`, map each id above to `window.goToPage("<Display
Name>")` (see the table). Recommend a plain JS object literal keyed by the id.

## Window verb contract (fixed enum)

`present | fullscreen | restore | always_on_top | normal | raise | hide_to_tray`

Semantics (for E4, not implemented here):
- `present` — raise + activate + (optionally) fullscreen with surfaces shown;
  the "AI takes over the screen" verb.
- `fullscreen` / `restore` — showFullScreen/showNormal, preserving prior
  maximize state.
- `always_on_top` / `normal` — WindowStaysOnTopHint flip.
- `raise` — bring to front without stealing full-screen state.
- `hide_to_tray` — minimize to tray icon.

## Stub sync needed at EV (per global DAG rule — `capture_views.cpp` is EV-only)

`src/ui/tools/capture_views.cpp`'s `StubApp` (used for headless view capture)
declares its own mirror of AppController's signal surface for QML binding in
capture mode. It is a separate class, not wired to the real AppController, so
none of the above changes break compilation there. For capture coverage of
future E4/E3 work, EV should add to `StubApp`:

```cpp
signals:
    ...
    void navigateRequested(QString);
    void windowRequested(QString);
    // (optional) extend surfaceRequested's stub signature to the full 12-param
    // form if a capture view needs to assert on caption/md/x/y/w/h/group.
```

No action required for A3's own acceptance criteria (schema round-trip is
unit-tested via `EventBus` spies in `tests/`, not via capture_views).

## Verification performed

- Read `src/ui/qml/Main.qml` (`pages` array, lines ~94-107) and
  `src/ui/qml/SurfaceHost.qml` (`open_page` no-op, lines ~140-142) to confirm
  the bug and derive the canonical page list.
- Checked all existing `SurfaceRequest`/`surfaceRequested` consumers
  (`agent_loop.cpp:dispatchSurfaceStep`, `AppController::spawnSurfaceDemo`,
  `tests/test_agent_e2e.cpp` C5 block, `tests/test_harness_e2e.cpp`
  `testSurfaceStep`) — all use field-by-field struct assignment or the struct
  reference in a lambda, so the new optional fields do not require any changes
  there and existing tests keep passing unmodified.
- No existing test exercised `open_page` before this change (grepped), so
  redirecting it from a no-op `SurfaceRequest` to a real `NavigateRequest`
  does not break any test.
