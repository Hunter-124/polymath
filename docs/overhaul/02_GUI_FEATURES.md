# 02 — GUI Feature Architecture

Functional features of the GUI overhaul: settings, command palette, notification center,
dashboard HUD plumbing, SurfaceHost. Complements 01 (visual system). All C++ follows the
existing patterns: service QObjects registered as context properties inside
`AppController::registerWithEngine()` (app_controller.cpp:219-237, NOT main.cpp), EventBus
queued connections for cross-thread marshaling.

**Ground rules (verified):**
- QML singletons (Style, Icons) cannot see context properties — Main.qml bridges values in.
- `event_bus.h` / `config.h` are included widely → editing them = wide pm_core recompile.
  All such edits happen in ONE foundation node (see 06_DAG A2).
- Every new context property / Q_PROPERTY / Q_INVOKABLE the QML binds must be stubbed in
  `src/ui/tools/capture_views.cpp` or headless captures crash.

## Feature 1 — SettingsView + SettingsController

**New C++** `src/ui/settings_controller.h/.cpp` (lives in pm_ui beside the models; owned by
AppController; registered as context property `settings`; holds `Database&` + `Config&`).

```cpp
class SettingsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent           READ accent           WRITE setAccent           NOTIFY accentChanged)
    Q_PROPERTY(bool    effects          READ effects          WRITE setEffects          NOTIFY effectsChanged)
    Q_PROPERTY(double  effectsIntensity READ effectsIntensity WRITE setEffectsIntensity NOTIFY effectsIntensityChanged)
    Q_PROPERTY(double  fontScale        READ fontScale        WRITE setFontScale        NOTIFY fontScaleChanged)
    Q_PROPERTY(bool    reduceMotion     READ reduceMotion     WRITE setReduceMotion     NOTIFY reduceMotionChanged)
public:
    SettingsController(Database& db, Config& cfg, QObject* parent=nullptr);
    Q_INVOKABLE QString getString(const QString& key, const QString& def={}) const;
    Q_INVOKABLE int     getInt(const QString& key, int def=0) const;
    Q_INVOKABLE bool    getBool(const QString& key, bool def=false) const;
    Q_INVOKABLE double  getReal(const QString& key, double def=0.0) const;
    Q_INVOKABLE void    setString(const QString& key, const QString& v);   // + setInt/setBool/setReal
    Q_INVOKABLE QVariantList audioInputDevices() const;   // via QMediaDevices [{id,label}]
    Q_INVOKABLE QVariantList audioOutputDevices() const;
signals:
    void settingChanged(QString key, QVariant value);     // fired for every set*
    // + typed *Changed
};
```
- `set*`: write DB (Config), update cached typed value, emit typed + generic signal. Keys
  with a live backend consumer (wake word, web-search backend/key, audio devices) ALSO
  publish on EventBus (mirror `setPrivacy`, app_controller.cpp:300). Pure `ui.*` keys don't.
- UI thread residency; DB writes are bounded (same as setPrivacy/refresh*).
- Audio pickers use `QMediaDevices` (Qt6::Multimedia already linked); persist choice to
  `audio.input_device`/`audio.output_device`; AudioService honors them per 04 §5.

**New config keys** (`keys::` in config.h + `seedDefaults()` in config.cpp — single owner, node A2):

| Key | Default | | Key | Default |
|---|---|---|---|---|
| `ui.accent` | `#33E1FF` | | `audio.input_device` | `""` |
| `ui.effects` | `1` | | `audio.output_device` | `""` |
| `ui.effects_intensity` | `0.6` | | `app.start_minimized` | `0` |
| `ui.font_scale` | `1.0` | | `app.launch_on_login` | `0` |
| `ui.reduce_motion` | `0` | | `agents.allowed_dirs` | `""` (see 05) |
| `audio.asr_idle_unload_s` | `90` (04 §5) | | `agents.max_concurrent` | `2` (see 05) |

**Style bridge (the one hand-off point between visual + functional systems)** — Main.qml:
```qml
Binding { target: Style; property: "accent";           value: settings.accent }
Binding { target: Style; property: "fontScale";        value: settings.fontScale }
Binding { target: Style; property: "effectsEnabled";   value: settings.effects && pmEffectsEnabled }
Binding { target: Style; property: "effectsIntensity"; value: settings.effectsIntensity }
Binding { target: Style; property: "reduceMotion";     value: settings.reduceMotion }
```

**QML** `qml/SettingsView.qml` (+ optional `qml/SettingsSection.qml` helper): scrolled
sections — Appearance (accent swatches, effects toggle+intensity PmSlider, font scale,
reduce motion), Audio (device PmComboBoxes, wake word phrase), Web Search (backend combo +
key field), Behavior (quiet hours, retention days, startup), Agents (allowed dirs,
max concurrent — see 05). Privacy stays in PrivacyView (master-gate semantics live there);
Settings shows a link-row to it. Accepts `property string focusSection` so the command
palette can deep-link.

## Feature 2 — Command palette (Ctrl+K)

**All-QML.** `qml/CommandPalette.qml` — reusable glass modal (GlassSurface, elev3):
- props: `actions` (list), internal `query/filtered/selectedIndex`.
- JS fuzzy scorer: subsequence match; bonus for consecutive, word-boundary, and prefix hits;
  −1 = no match; rank desc; show top 12 grouped by `section`.
- Keys: Up/Down navigate, Enter runs, Esc closes; TextField grabs focus on open.

**Action registry lives in Main.qml** (closures need `app`, `pageHost`, `settings`,
`notifications` — invisible to singletons):
```qml
readonly property var paletteActions: [
  // nav.* — one per pages[] entry (auto-derived)
  { id:"ptt.toggle",    title:"Toggle push-to-talk",     section:"Voice",  run:()=>… },
  { id:"chat.focus",    title:"Focus chat input",        section:"Chat",   run:()=>… },
  { id:"shop.add",      title:"Add shopping item…",      section:"Create", run:()=>… },
  { id:"settings.appearance", title:"Settings: Appearance", section:"Settings", run:()=>openSettings("appearance") },
  { id:"ui.effects.toggle", title:"Toggle glass effects", section:"Appearance", run:()=>settings.effects=!settings.effects },
  { id:"surface.demo",  title:"Spawn placeholder surface", section:"Dev",  run:()=>app.spawnSurfaceDemo() },
  { id:"agent.spawn",   title:"New agent session…",      section:"Agents", run:()=>… /* 05 */ },
  { id:"app.quit",      title:"Quit Polymath",           section:"System", run:()=>Qt.quit() },
]
property var dynamicActions: []          // future AI-registered actions
function registerAction(a) {…}  function unregisterAction(id) {…}
// palette.actions = paletteActions.concat(dynamicActions)
Shortcut { sequence: "Ctrl+K"; onActivated: palette.open() }
```

## Feature 3 — Notification center

**New C++** `src/ui/models/notifications_model.h/.cpp` — `NotificationsModel :
QAbstractListModel`, context property `notifications`, owned by AppController, wired in
`wireModels()`:
- Roles: id, severity, source, title, body, timestamp, timeLabel, read, category.
- `Q_PROPERTY(int unreadCount NOTIFY unreadCountChanged)`;
  `Q_INVOKABLE markAllRead()/markRead(id)/clearAll()/refreshFromEvents()`.
- Slots (queued from EventBus): `onNotice(Notice)`, `onTask(TaskEvent)`,
  `onReminder(ReminderFired)`, `onDetection(Detection)`.
- In-memory ring buffer cap 200; no new table — `refreshFromEvents()` seeds last ~50 rows
  from the existing `events` table (ActivityLog) at startup.
- Existing toast chain (notice → noticePosted) is untouched; toasts + model are independent
  consumers.

**QML**: `qml/ToastStack.qml` (replaces the single toast: bottom-anchored, max 3 visible,
slide/fade, severity color bar, 4–6 s, reduceMotion-aware; fed by the same
`app.onNoticePosted`), `qml/NotificationBell.qml` (titlebar PmToolButton + unread PmBadge),
`qml/NotificationCenter.qml` (glass popover ListView over `notifications`; severity dot,
source, title, timeLabel; unread emphasized; "Clear all"; row click → navigate for
task/reminder categories + markRead; open → markAllRead).

## Feature 4 — Dashboard HUD plumbing

**AppController additions** (wired in `wireEventBus()`):
```cpp
Q_PROPERTY(int vramUsedMiB  READ vramUsedMiB  NOTIFY vramChanged)
Q_PROPERTY(int vramTotalMiB READ vramTotalMiB NOTIFY vramChanged)
signals: void vramChanged(); void wakeWordPulse();
// connect(inference_.get(), &InferenceManager::vramChanged, …cache+emit…)   // first consumer
// connect(audio_.get(), &AudioService::wakeWordHeard, …emit wakeWordPulse…) // first consumer
```
Already available, no new plumbing: `app.listening`, taskModel counts, `app.models()`
(`active` field), `app.modelStatus`, `app.activePersonality`.
`Dashboard.qml` adds: VRAM gauge (used/total bar, warn >85 %), wake-word ping animation,
task counts (running/queued), resident-model chips, agent-session summary (from
`agentSessions` model once 05 lands — bind defensively with `typeof !== "undefined"`).

## Feature 5 — SurfaceHost foundation

**EventBus** (node A2): payload struct + signal + `publishSurfaceRequest` helper + metatype:
```cpp
struct SurfaceRequest { QString id, action /*spawn|close|arrange|open_page*/,
                        type /*placeholder|image|web|video|monitor*/, title, args_json; };
```
**AppController relay**: signal `surfaceRequested(QString id, QString action, QString type,
QString title, QString argsJson)` connected from the bus; plus
`Q_INVOKABLE void spawnSurfaceDemo()` publishing a placeholder request (proves
publish → relay → host end-to-end with no backend).

**QML** `qml/SurfaceHost.qml`: overlay region; listens to `app.onSurfaceRequested`; model of
live surfaces; `spawn` → instantiate via type→component map (Loader), `close` → remove by
id, `arrange` → tile (minimal). Surface components in `qml/surfaces/`:
- `PlaceholderSurface.qml` — titled glass panel echoing args.
- `ImageSurface.qml` — image from args.url/path.
- `WebSurface.qml` — "WebEngine not installed" glass message; when QtWebEngine is later
  installed only this file changes (embed WebEngineView; adblock via
  UrlRequestInterceptor; YouTube-cleanup via injected scripts) — see 06_DAG D5.

**`ui_control` tool JSON schema (spec — implemented in node C5):**
```jsonc
{ "name": "ui_control",
  "description": "Compose the on-screen layout: open pages, spawn/close/arrange surfaces.",
  "parameters": { "type":"object", "properties": {
      "action": { "enum":["open_page","spawn_surface","close_surface","arrange"] },
      "page":   { "type":"string" }, "id": { "type":"string" },
      "type":   { "enum":["placeholder","image","web","video","monitor"] },
      "title":  { "type":"string" }, "args": { "type":"object" },
      "layout": { "enum":["tile","stack","split-left","split-right","full"] } },
    "required":["action"] } }
```
Tool invoke() (worker thread) → `EventBus::publishSurfaceRequest` — the sanctioned
worker→UI route (mirrors camera_tools.cpp:88). Registered in register_tools.cpp; gated
per-persona via the `tools` allow-list.

## capture_views.cpp stub additions (node A2 — mandatory)

- `StubSettings` (ctx `settings`): typed props with WRITE+NOTIFY (accent "#33E1FF",
  effects true, effectsIntensity 0.6, fontScale 1.0, reduceMotion false); generic get*/set*
  invokables (get returns def); audio device lists returning samples; settingChanged signal.
- `StubNotifications` (ctx `notifications`): StubListModel extension with roles above,
  3–4 sample rows, `unreadCount` Q_PROPERTY, markAllRead/markRead/clearAll/refreshFromEvents.
- `StubApp` additions: vramUsedMiB 5400 / vramTotalMiB 8192 (+notify), signals
  wakeWordPulse / surfaceRequested(5×QString), `spawnSurfaceDemo()` no-op,
  `pmEffectsEnabled=false` context property.
- Add `SettingsView` (and later `AgentSessionsView`, 05) to the capture `views[]` table.

## Execution order (mirrors 06_DAG)

1. **A2 foundation** (one agent): event_bus + config + SettingsController +
   NotificationsModel + AppController wiring + CMake + capture stubs → full build green.
2. **Parallel QML**: SettingsView · Toast/Bell/Center · CommandPalette · SurfaceHost+surfaces
   · Dashboard HUD (each owns only its own new files).
3. **C1 shell integration** (one agent, last): Main.qml only — palette + Shortcut, ToastStack
   swap, bell+popover, SurfaceHost, Settings/Agents nav entries, Style↔settings bridge.
