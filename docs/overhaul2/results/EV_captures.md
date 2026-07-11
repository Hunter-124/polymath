# EV — Wave-E stub sync + capture verify

**Owned file:** `src/ui/tools/capture_views.cpp` only.  
**Result note:** this file. No QML / app_controller edits. No build run here
(orchestrator builds centrally).

## Applied from stub notes

### B2 (`B2_stubs.md`) — VideoPickerSurface
- Extended `View` with `extraOnLoaded` (and `customWindow` for E3 board).
- `renderView` Loader runs optional Ready-path JS to set surface props.
- `views[]` entry `14-video-picker` → `surfaces/VideoPickerSurface.qml` with
  `kVideoPickerSeedJs` (6 fake `youtube_search` results; blank `thumbnailUrl`
  for offline-stable “No thumbnail” path).
- **Deferred / skipped (as recommended by B2):** `WebSurface.qml` / type
  `"video"` (QtWebEngine not initialized in this harness).

### C1 (`C1_stubs.md`) — confirm + notification roles
- `StubApp`: `confirmRequested(...)`, `respondConfirm(...)`, `confirmSettled(id)`.
- `StubNotifications` roles: `pendingAction`, `confirmId` (plus existing).
- Invokables: `approveConfirm` / `denyConfirm` (no-ops).
- Seeded pending confirm row `cap-confirm-1` (category `confirm`).
- `StubSettings.getString` / `getBool` return Safety defaults for
  `safety.mode`, `safety.fs_allowed_roots`, `safety.cmd_denylist`,
  `safety.tool_overrides`, `safety.audit`.
- **Not done:** live ConfirmDialog PNG via timed `emit confirmRequested`
  (optional demo; Main already has Connections; not required for crash-free
  load). NotificationCenter Approve/Deny chrome remains UI follow-up (C1).

### D1 (`D1_stubs.md`) — scheduled goals
- `StubApp.refreshSchedules()` no-op invokable.
- Seeded `scheduledGoalsModel` (`StubListModel`, 3 rows when populated).
- Context property `scheduledGoalsModel` in `renderView`.
- `StubListModel.setEnabled(int, bool)` no-op for QML toggle/trash path.
- No new `views[]` entry (`05-tasks` already covers TaskQueueView).

### D4 (`D4_stubs.md`) — TTS on StubSettings
- Properties: `ttsEngine`, `ttsVoice`, `ttsSpeed`, `ttsVolume` (+ writers/signals).
- Invokables: `ttsVoices()` (3 fake rows), `previewVoice()`.
- Defaults: engine `auto`, voice `af_heart`, speed/volume `1.0`.

### E2 (`E2_stubs.md`) — personalities
- Seeded `personalityModel` (Assistant / Marcus Aurelius / Ada Lovelace).
- Context property `personalityModel`.
- `StubListModel.indexOfName` + `get(row)`.
- `StubApp`: `createPersonality`, `savePersonality`, `setPersonalityAvatar`,
  `deletePersonality`, `availableToolNames()`, `availableModels()`.
- `views[]`: `08b-personality-editor` → `PersonalityEditor.qml` (create-new
  state; `personaName` default empty).
- **Nice-to-have deferred:** second capture with `personaName: "Marcus Aurelius"`
  for edit-state PNG.

### E3 (`E3_stubs.md`) — notes + board
- `15-note-surface` with markdown seed (`kNoteSeedJs`).
- `16-image-surface` with title/caption seed (empty source → placeholder).
- `17-placeholder-surface` bare entry.
- `18-surface-board` custom Window QML (`kBoardDemoQml`): 2 notes + 2 images
  + 1 `video_picker` in groups “Castles” / “Trip planning”, then
  `host.arrange("board")`.
- Media stand-in is `video_picker` not WebEngine `video` (headless-safe),
  matching B2/E3 guidance.

### E4 / A3 signals
- `StubApp`: `navigateRequested(QString)`, `windowRequested(QString)`.
- `surfaceRequested` stub extended to full 12-param A3 signature
  `(id, action, type, title, argsJson, caption, md, x, y, w, h, group)`.
- No present-pill / takeover demo emission (Main uses
  `ignoreUnknownSignals: true`; signals exist for future scripted demos).

## Crash-prevention checklist (minimum)

| Surface | Status |
|--------|--------|
| `navigateRequested` / `windowRequested` | present |
| `confirmRequested` / `respondConfirm` / `confirmSettled` | present |
| `refreshSchedules` | present |
| personality CRUD + tool/model lists | present |
| `tts*` props + `ttsVoices` | present |
| `personalityModel` / `scheduledGoalsModel` context | present |
| Seeded models for populated mode | present |
| `indexOfName` / `get` on StubListModel | present |
| views: PersonalityEditor, VideoPicker, NoteSurface | present |

## Deferred / partial

| Item | Why |
|------|-----|
| WebSurface / playing embed capture | Needs QtWebEngine process init; flaky offscreen — B2/E3 say skip |
| ConfirmDialog dedicated PNG | Optional emit after Main load; not crash-critical |
| PersonalityEditor *edit* state | Create-new capture is enough for accept; edit wrapper optional |
| Present-pill / `windowRequested("present")` demo | Signals exist; scripted Main takeover PNG not required this wave |
| NotificationCenter Approve/Deny buttons | Model ready; chrome is separate UI pass |

## Capture map (populated PNGs)

| PNG stem | Source |
|----------|--------|
| 01-main-shell … 13-agents | existing shell/views (+ new models/signals) |
| 08b-personality-editor | PersonalityEditor (new) |
| 14-video-picker | VideoPickerSurface seeded |
| 15-note-surface | NoteSurface seeded |
| 16-image-surface | ImageSurface seeded |
| 17-placeholder-surface | PlaceholderSurface |
| 18-surface-board | SurfaceHost board demo |

## Verify (orchestrator)

```powershell
cmake --build build/cpu --config Release --target capture_views
$env:QT_QPA_PLATFORM='offscreen'
build/cpu/bin/Release/capture_views.exe <outdir>
# optional empty states:
build/cpu/bin/Release/capture_views.exe <outdir> --empty
```
