# C3 — Screen capture + describe

## Files changed (C3-owned only)

| Path | Change |
|------|--------|
| `src/agent/tools/screen_tools.h` | **NEW** — `ScreenCaptureTool`, `ScreenDescribeTool` |
| `src/agent/tools/screen_tools.cpp` | **NEW** — Qt grab + PNG save + optional VLM describe |
| `src/core/config.h` | `keys::ScreenCapture` = `privacy.screen_capture` |
| `src/core/config.cpp` | seed default `"1"`; add to `isMasterGated` |
| `docs/overhaul2/results/C3_register.md` | registration / CMake / test-count for merge |
| `docs/overhaul2/results/C3_notes.md` | this file |

**Not edited (by design):** `register_tools.cpp`, `src/agent/CMakeLists.txt` (C2),
`capture_views.cpp` (EV), Privacy QML (E5), `src/vision/*` (existing
`InferenceManager::describeImage` is sufficient).

## Tools

### `screen_capture` (Risk Read)

Args: `{ monitor?: int, window_title?: string }`

1. Privacy gate: `privacy.screen_capture` must be on (default ON). Missing key
   treated as ON. Respects `privacy.master_enabled` via `isMasterGated`.
   Denied error text: **`screen capture disabled in Privacy settings`**.
2. Grab via `QScreen::grabWindow`:
   - default → primary screen (`grabWindow(0)`)
   - `monitor` → `QGuiApplication::screens().at(monitor)` (bounds-checked)
   - `window_title` → Windows only: case-insensitive partial title match via
     `EnumWindows` + `grabWindow(HWND)`; not found → error. Non-Windows: falls
     back to primary with a `note` field.
3. Save PNG: `{Paths::instance().root()}/captures/screen_YYYYMMDD_HHmmss_zzz.png`
   (creates `captures/` if needed).
4. Best-effort prune: deletes `screen_*.png` older than **7 days** in that dir.
5. Returns `{ ok, path, width, height, ui_hint }` (+ optional `note`).

`ui_hint` is a ready-made spawn suggestion:

```json
{
  "action": "spawn_surface",
  "type": "image",
  "title": "Screen capture",
  "args": { "path": "<absolute path>" }
}
```

### `screen_describe` (Risk Read)

Same capture path as above, then:

1. Build a `Frame` (JPEG bytes from the QImage) and call
   `ctx.inference->describeImage(frame, prompt)` when `ToolContext.inference`
   is set.
2. **Fallback (no crash):** if inference is null, encode fails, no vision model
   is registered, VLM not compiled, or describe returns empty:

   > `vision model not loaded; image saved at <path>`

   Result still `ok: true` with `path` + `vision_ok: false` + `vision_note`.
3. On success: `description` text, `vision_ok: true`, summary = description.

## Capture path layout

```
<Paths root>/
  captures/
    screen_20260710_143022_123.png
    ...
```

`Paths::root()` is the app data root (portable `data/` next to exe, or
`%LOCALAPPDATA%/Polymath`). Layout is created on demand by the tool (no change
to `Paths::ensureLayout()`).

## Describe fallback behavior

| Condition | Result |
|-----------|--------|
| Privacy off / master off | `ok: false`, error string above |
| Grab / save fails | `ok: false`, `error` detail |
| Capture OK, vision unavailable | `ok: true`, PNG path kept, `vision_note` explains |
| Capture OK, VLM returns text | `ok: true`, `description` set |

No vision service hook was added — `InferenceManager::describeImage` already
loads Vision on demand (VRAM-budgeted).

## Downstream (not C3)

- **C2 / orchestrator:** merge `C3_register.md` (include, two `reg.add`, CMake
  one-liner, e2e tool count **+2**).
- **E5:** PrivacyView toggle row for `privacy.screen_capture`.
- **EV:** no capture_views stub required (no new QML surface).
