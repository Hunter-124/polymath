# Overhaul 2 — The Monolithic DAG

Everything that should be done, as an executable dependency graph. Waves are
topological layers; nodes inside a wave with disjoint file ownership may run as
parallel subagents. Node IDs are stable — commits are `overhaul2(<id>): …`.

```
WAVE A (harness correctness)      A1 ──► A2 ──► A4        A3 (parallel)
                                   │      │      │
WAVE B (YouTube, after A1/A2)     B1  B2  B3 ──► B4 (needs B1+B2+A2)
                                   │
WAVE C (computer use, after A4)   C1 ──► C2      C3 (parallel w/ C2)
                                   │
WAVE D (expansion, after A2/C1)   D1  D2(after A2,D1)  D3(after C3,D1)  D4 (independent)
                                   │
WAVE E (GUI, after A3)            E1  E2  E3  E4  E5   (all parallel)  ──► EV
                                   │
WAVE F (verify & ship)            F1 ──► F2 ──► F3
```

## Global rules (identical to Overhaul 1 — they worked)

- **One node = one owner = one commit** (`overhaul2(<id>): <summary>`) that also ticks
  `PROGRESS.md`. Disjoint file ownership within a wave; the ownership list in each node
  is exhaustive for *edits* (reading anything is fine). If a node discovers it must edit
  a file owned by a concurrent node, STOP and serialize.
- `src/ui/tools/capture_views.cpp` is owned by **EV only** (wave E) and **F1** (wave F).
  Nodes that add QML↔C++ surface area write their required stub changes into
  `docs/overhaul2/results/<id>_stubs.md` instead of editing capture_views directly —
  EV applies them all at once. (Avoids the #1 merge conflict from Overhaul 1.)
- `SettingsView.qml` / `settings_controller.cpp`: C1 owns the Safety section (wave C),
  D4 owns the Voice section (wave D), never concurrently.
- `agent_loop.cpp` is owned by the sequential chain A1→A2→A4, then D2. Never parallel.
- New config keys go in `src/core/config.h` `keys` namespace + `seedDefaults()`.
  Cross-thread = EventBus only. QML colors/sizes = `Style.qml` tokens only.
- Every node: build `capture_views` target green + affected-view PNGs sane before commit.
  `ctest` for nodes touching C++ logic with existing tests; add tests where a spec says so.
- Verification artifacts / deviation notes → `docs/overhaul2/results/<id>_*.md`.

---

# WAVE A — Harness correctness (fix what's broken before adding anything)

## A1 — Final-answer hygiene + Router v2
**Owns:** `src/agent/agent_loop.cpp`, `agent_loop.h`, `src/agent/prompts.*` (if split out).
**Deps:** none.

1. **Kill the tool-call leak (B-LEAK):**
   - Final unconstrained generation in `runQuick` (`:934-949`) must use a context with
     `include_tool_protocol=false` — a clean "answer the user in plain prose" system
     prompt (persona + memory + history, no JSON instructions).
   - Post-process the final text: if it parses as a tool call (`parseToolCall` succeeds),
     either (a) `final_answer` → extract `arguments.answer`; (b) another known tool →
     execute it (one bonus round) and regenerate the prose answer; (c) unknown/malformed
     JSON blob → strip to any non-JSON prose or synthesize "…done" summary from tool
     results. **Raw JSON must never be published** via `streamOrPublishAnswer`.
   - Streaming caveat: buffer the first ~24 chars of the final stream; if it starts with
     `{`, hold the stream and fall to the post-process path instead of live-streaming.
   - Same sanitization on the `finalAnswer` fallback (`:940-945`).
2. **Router v2 (B-ROUTE):**
   - Replace the hardcoded substring list (`:716-719`) with: (a) trigger matching against
     `SkillRegistry` triggers (word-boundary, case-insensitive, allow gaps — "open a
     youtube video" matches trigger "open youtube"); (b) a small intent-verb table
     (open/play/put on/show/watch/launch + media/site object) → Command; (c) existing
     Quick/Goal heuristics unchanged.
   - **Unify skill loading:** delete the parallel `tryExpandSkill` loader (`:168`);
     `runCommand` uses `SkillRegistry::expand` (one loader, one directory scheme).
   - `runCommand` handles *any* matched skill (not just slop_mode), and falls back to
     Quick (not the canned "Understood — I'll treat that as a command" string, which dies).
3. **Tests:** extend `tests/test_agent_loop` (or add `test_router`): route
   classification table incl. "open a youtube video for me", "play some lofi",
   "add milk to the list" (→Quick tool), "what's 2+2" (→Quick); final-answer
   sanitizer unit tests (tool-call JSON in → prose out).

**Accept:** unit tests green; live CPU run: "open a youtube video for me" routes to
Command (log line), no raw JSON ever appears in `ChatModel` for 10 varied prompts.

## A2 — Goal execution integrity (run_skill, resume, session rejoin)
**Owns:** `src/agent/agent_loop.cpp/.h` (after A1 lands), `src/agent/tools/skill_tools.cpp`,
`src/agent/agent_runtime.cpp`.
**Deps:** A1.

1. `run_skill` (`skill_tools.cpp:68-95`): after persisting goal+steps, hand off for
   execution — publish a `GoalReady` event / direct call so `AgentLoop` executes it
   (same-thread queued invoke). No more silent parked goals (B-DEADGOAL).
2. Call `resumeActiveGoals()` on startup (from `AgentRuntime::start`), and after any
   goal finishes, scan for next runnable goal (simple FIFO, one at a time).
3. **Session rejoin:** subscribe `AgentLoop` to `AgentSessionEvent`; a session
   completion/failure resumes the goal parked `waiting_agent` on it
   (`dispatchAgentSessionStep` `:1432`), injecting the session transcript tail/result
   summary as the step result. Timeout: goals `waiting_agent` > `agents.join_timeout_min`
   (default 120) get a reflect round instead of hanging forever.
4. Risk-gate *plumbing* (enforced in A4): dispatch paths route through one
   `dispatchToolChecked(tool, args, ctx)` helper — single choke point for A4/C1.

**Accept:** `run_skill slop_mode` from chat actually spawns the surface; kill the app
mid-goal → restart → goal resumes (manual e2e, note in results/); a spawned
`agent_spawn` session finishing resumes its parked goal (fake/pty provider ok in test).

## A3 — ui_control schema v2 (open_page fix + window verbs + surface args)
**Owns:** `src/agent/tools/uicontrol_tool.cpp/.h`, `src/core/event_bus.*` (SurfaceRequest
payload extension), `src/app/app_controller.cpp/.h` (relay only).
**Deps:** none (schema/plumbing only; QML handlers land in E4).

1. `open_page` → publish a `NavigateRequest` (page id from a fixed enum of nav rail
   pages) that `AppController` relays to a QML signal (Main.qml handler = E4).
2. New `window` action: `{action:"window", verb:"present|fullscreen|restore|
   always_on_top|normal|raise|hide_to_tray"}` → relayed as `windowRequested(verb)`.
   "present" = raise+activate+optionally fullscreen with surfaces shown — the
   "AI takes over the screen to show you things" verb.
3. `spawn_surface` args extended: `title`, `caption`, `md` (markdown body for the new
   text card), `w/h/x/y` hints, `group` (research-board grouping key). Backward compatible.
4. Tool spec text rewritten so a small local model reliably picks correct verbs
   (few-shot examples in the description, tight enums).

**Accept:** tool schema round-trips (unit test in `tests/`); `open_page` publishes
NavigateRequest (observable in test via EventBus spy); no QML edits in this node.

## A4 — Risk-gate enforcement (SafetyPolicy core + waiting_user)
**Owns:** `src/agent/agent_loop.cpp/.h` (after A2), `src/core/safety_policy.cpp/.h`
(new), `src/agent/tool_registry.cpp/.h`, `src/core/config.h/.cpp` (keys).
**Deps:** A2.

1. New `SafetyPolicy` (core, thread-safe, config-backed):
   - `Decision check(toolName, riskClass, argsJson)` → `Allow | Confirm | Deny(reason)`.
   - Config keys: `safety.mode` (`strict|standard|trusted`, default `standard`),
     `safety.autoconfirm_risk_max` (default: WriteLocal), `safety.fs_allowed_roots`
     (default: Documents + Desktop + Downloads + data dir), `safety.fs_denied_globs`
     (defaults protect `.git`, `AppData`, `Windows`, `Program Files`, the Polymath DB),
     `safety.cmd_denylist` (regex list: `format`, `del /s`, `rd /s`, `rm -rf`,
     `Remove-Item -Recurse`, registry writes, `shutdown`, disk tools…),
     `safety.max_file_write_kb` (default 2048), `safety.audit` (default on).
   - Deny always wins over Confirm; Destructive is NEVER auto-confirmed in any mode.
2. Enforce at the single choke point from A2-§4: `Allow` → invoke; `Deny` → tool error
   result the model sees ("denied by safety policy: <reason>") so it can adapt;
   `Confirm` → park goal/turn `waiting_user`, publish `ConfirmRequest{id, tool, summary,
   argsPreview}` on the bus, resume on `ConfirmResponse{id, approved}`. In runQuick
   (no goal), the turn ends with a chat prompt "⚠ Needs your approval: …" and the
   pending call is persisted so "yes, do it" / notification-approve resumes it.
3. Every gated invocation (allowed or not) appends to the existing `ActivityLog` with
   decision + reason.
4. Tests: `tests/test_safety_policy` — path allow/deny matrix, cmd denylist, mode
   escalation, Destructive-never-auto.

**Accept:** tests green; `print_document` (Spend) now requires approval end-to-end in a
live run; denial reasons visible in chat; audit rows written. (Approval UI is a chat
message + notification action now; rich dialog lands in C1.)

---

# WAVE B — YouTube pipeline (owner's top priority)

## B1 — `youtube_search` tool
**Owns:** `src/agent/tools/youtube_search.cpp/.h` (new), registration line in
`register_tools.cpp`, `tests/test_youtube_search*`.
**Deps:** A1 (uses its answer path but independent code; may start in parallel).

1. No API key: POST the public Innertube endpoint
   (`https://www.youtube.com/youtubei/v1/search` with the web client context) via the
   existing Qt network stack (`fetch_page.cpp` patterns); fallback = GET
   `/results?search_query=` and parse `ytInitialData` JSON out of the HTML.
2. Returns top N (default 6): `{videoId, title, channel, durationSec, views,
   publishedText, thumbnailUrl, watchUrl}` — compact JSON the Fast model can reason over.
3. Args: `{query, max_results?, order?}`. Risk: External (auto-allowed by default policy).
4. Robustness: 5s timeout, graceful "youtube unreachable" tool error; parser tolerates
   missing fields; unit test against a stored fixture of `ytInitialData` (no network in
   tests).

**Accept:** fixture test green; live call returns ≥3 sane results for "lofi hip hop".

## B2 — Video surface v2 + results picker
**Owns:** `src/ui/qml/surfaces/WebSurface.qml`, new `src/ui/qml/surfaces/
VideoPickerSurface.qml`, `src/ui/qml/SurfaceHost.qml` (typeMap + spawn handling only).
**Deps:** A3 (extended spawn args).

1. WebSurface video mode: prefer `youtube-nocookie.com/embed/<id>?autoplay=1` for
   `videoId` spawns (fewer ads/overlays than watch pages); keep watch-URL support.
   Title bar shows video title (from args); add a mute/back/close control row.
2. `VideoPickerSurface` (`type:"video_picker"`): grid of thumbnail cards
   (thumb, title, channel, duration) from an args-supplied results array; clicking a
   card publishes a `SurfaceRequest` replacing the picker with the playing video.
   This is the "model searched, user selects" flow.
3. SurfaceHost: register the new type; `video_picker`→video replacement keeps the same
   layout slot.
4. Write stub requirements to `docs/overhaul2/results/B2_stubs.md` (per global rule).

**Accept:** capture PNG of VideoPickerSurface with 6 fake results looks clean; manual:
clicking a card swaps to playing embed.

## B3 — Adblock + clean-mode hardening
**Owns:** `src/ui/web_adblock_interceptor.cpp/.h`, the `ytCleanScript` block inside…
⚠ `WebSurface.qml` is owned by B2 — so B3 delivers the improved clean script as
`src/ui/qml/surfaces/YtClean.js` (new file) + interceptor changes; B2 imports it.
**Deps:** none (coordinates with B2 via the new JS file contract).

1. Interceptor: extend host list (current YouTube ad/analytics hosts, `*.doubleclick`,
   `googleads`, `adservice`), keep googlevideo `ctier=L`/`oad=` stream heuristics; add
   an optional on-disk extra-hosts file (`data/adblock_extra.txt`) loaded at start.
2. `YtClean.js`: CSS hide list refreshed for 2026 YouTube DOM (`ytd-ad-slot-renderer`,
   engagement panels, masthead ads); skip-button auto-click; ad-showing mute; also runs
   on embed pages; SponsorBlock-style segment skipping is **Z-backlog**, not here.
3. Unit-testable pure function for URL classification (extract to
   `web_adblock_rules.cpp` if needed) + test.

**Accept:** test green; manual: a monetized watch page + an embed page play with no
pre-roll visible or audible in ≥3 of 3 tries (note results in results/B3_verify.md).

## B4 — `watch_video` skill + end-to-end wiring
**Owns:** `data/skills/watch_video/skill.json` (new), `data/skills/slop_mode/skill.json`
(update to use search), prompt/catalog text files if any.
**Deps:** B1, B2, A2 (skills actually execute now).

1. Skill `watch_video`: triggers ("watch", "play video", "open youtube", "put on",
   "show me a video"); params `{topic}`; steps: `youtube_search{query:topic}` →
   `ui_control spawn_surface{type:"video_picker", args:results}` — or, when
   `params.autoplay==true` (slop mode), spawn the top result directly as `video`.
2. `slop_mode` updated: search "lofi/ambient/background" style query → autoplay top
   result (real video, not a results page).
3. Router triggers verified against A1's matcher.

**Accept:** LIVE e2e on GPU build: saying/typing "open a youtube video about castles"
→ picker appears with real results → click → ad-free playback. "slop mode" → a video
just plays. This is the owner's #1 acceptance test.

---

# WAVE C — Scripted computer use + safeguards

## C1 — Confirmation UX + Safety settings UI
**Owns:** `src/ui/qml/ConfirmDialog.qml` (new), `src/ui/qml/Main.qml` (dialog host +
Connections only), `src/ui/qml/SettingsView.qml` + `src/ui/settings_controller.cpp/.h`
(Safety section), `src/app/app_controller.cpp/.h` (ConfirmRequest relay),
`src/ui/models/notifications_model.*` (approve/deny actions).
**Deps:** A4.

1. Glass confirm dialog: tool name, human summary, args preview (pretty JSON, path
   diffs for fs_write), Approve / Deny / "Always allow this tool" (writes a
   `safety.tool_overrides` entry). Fires from `ConfirmRequest`; responds
   `ConfirmResponse`. Voice approval ("yes do it") flows through the existing chat path
   from A4.
2. Notification center entries mirror pending confirmations (approve/deny inline).
3. Settings ▸ Safety: mode selector (strict/standard/trusted with plain-English
   descriptions), allowed roots editor (list + add/remove), denylist viewer, audit
   toggle, "recent gated actions" list from ActivityLog.

**Accept:** capture PNGs (dialog + settings section); manual approve + deny + always-
allow round-trips; approval resumes the parked goal.

## C2 — System tools (the computer-use surface)
**Owns:** `src/agent/tools/system_tools.cpp/.h` (new: fs + process + clipboard),
`src/agent/tools/register_tools.cpp` (registration), `tests/test_system_tools*`.
**Deps:** A4 (SafetyPolicy exists), C1 in parallel is fine (chat-approval path from A4
suffices until C1 lands).

New tools, all routed through SafetyPolicy:
- `fs_list {path}` (Read) · `fs_read {path, max_kb?}` (Read, denied outside roots) ·
  `fs_write {path, content, mode:create|overwrite|append}` (WriteLocal; overwrite of an
  existing file = Confirm; creates parent dirs inside roots only) ·
  `fs_move {src,dst}` / `fs_delete {path}` (**Destructive** → always Confirm; delete =
  send to Recycle Bin via `IFileOperation`/SHFileOperation, never hard delete).
- `run_command {command, cwd?, timeout_s?}` (Destructive-class by default →
  Confirm; PowerShell `-NoProfile -NonInteractive`; stdout+stderr captured, truncated
  to 8k for the model; cwd must be inside allowed roots; denylist regex pre-scan;
  `safety.mode==trusted` may downgrade to Confirm-free for read-only-looking commands —
  keep the heuristic conservative).
- `app_launch {name_or_path, args?}` (External → Confirm in standard mode; resolves
  via Start-Menu/`shell:AppsFolder` search then PATH).
- `clipboard_read` (Read) / `clipboard_write {text}` (WriteLocal).
- Tool spec descriptions written for a small local model (short, example-rich).

**Accept:** `tests/test_system_tools` green (temp-dir sandbox: roots honored, recycle-
bin delete, denylist blocks `rm -rf`, overwrite confirms); live: "create a file
notes.txt on my desktop saying hi" → confirm → file exists; "delete it" → confirm →
in Recycle Bin.

## C3 — Screen awareness (`screen_capture` + describe)
**Owns:** `src/agent/tools/screen_tools.cpp/.h` (new), registration line,
`src/vision/vision_service.*` ONLY IF a describe hook is missing (prefer existing
vision describe path; note: vision model swaps with Fast per VRAM budget — acceptable,
it's on-demand).
**Deps:** A4.

1. `screen_capture {monitor?|window_title?}` (Read + privacy-gated by a new
   `privacy.screen_capture` master switch, default ON per owner intent, visible in
   Privacy view — key + seed only here; Privacy UI row is E5's one-line add): grabs via
   Qt `QScreen::grabWindow` (or DXGI later, Z-backlog); saves PNG under data/captures;
   returns path + dimensions.
2. `screen_describe {same args}`: capture → run the on-demand vision model (existing
   InferenceManager vision tier) → returns text description. This is the advisor's
   "glance at what you're doing".
3. Captured images are spawnable: tool result includes a ready-made `ui_control`
   suggestion (image surface path) so the model can show what it saw.

**Accept:** live: "what's on my screen right now?" → sane description; captures land
in data dir and are auto-pruned (>7 days, reuse retention config pattern).

---

# WAVE D — Harness expansion

## D1 — Scheduler v2: timed + recurring agent goals
**Owns:** `src/scheduler/proactive_engine.cpp/.h`, `src/scheduler/scheduler_util.*`,
`src/core/schema.h` (new `scheduled_goals` table), `src/agent/tools/schedule_tools.cpp`
(new: `schedule_task`/`list_schedules`/`cancel_schedule`; absorb/deprecate
`queue_deep_task`), `src/ui/models/task_model.*` + `src/ui/qml/TasksView.qml`
(schedules section).
**Deps:** A2 (goals execute reliably).

1. `scheduled_goals` table: `{id, title, prompt_or_skill, params_json, rrule|at|
   every_s, next_fire, last_fire, enabled, deliver:(chat|voice|notify), created_by}`.
2. ProactiveEngine tick (existing 30s QTimer) fires due rows → creates a **real goal**
   through the A2 path (full plan/execute/reflect + tools) tagged `source=schedule`;
   reschedules via existing `advanceRrule` (extend for `every_s` intervals + one-shot
   `at`). Quiet hours respected unless `deliver==notify`.
3. Tools: `schedule_task {title, when:{at?|every?|rrule?}, task:{skill?|prompt},
   deliver?}` (WriteLocal, Confirm if rrule/every — standing rules need a yes),
   `list_schedules`, `cancel_schedule {id}`.
4. TasksView: "Scheduled" section — list, next-fire countdown, enable toggle, delete;
   stub notes → results/D1_stubs.md.
5. Results delivered through the existing goal-terminal path (notification + chat line
   + optional TTS per `deliver`).

**Accept:** unit tests for rrule/interval advance incl. DST-safe local time; live:
"every morning at 8 give me a briefing" creates a schedule (visible in Tasks), firing
it manually (debug: `next_fire=now`) produces a spoken/chat briefing.

## D2 — Goal-tree orchestration (local subagents)
**Owns:** `src/agent/agent_loop.cpp/.h` (exclusive again), `src/core/schema.h`
(goals: `parent_id`, `join_policy`), `src/agent/tools/orchestration_tools.cpp` (new:
`spawn_subtask`, `subtask_status`).
**Deps:** A2, D1.

1. Goals get `parent_id`; a plan step `kind:"fanout"` (or the model calling
   `spawn_subtask` N times) creates child goals. Children that are pure-local run
   sequentially (one inference thread); children of kind `agent_session` run in
   parallel via existing AgentSessionService (bounded by `agents.max_concurrent`).
2. Parent parks `waiting_children`; last child completion (A2's rejoin machinery
   generalized) resumes parent with a synthesized results digest; `join_policy:
   all|any|first_success`.
3. Reflect round on partial failure (≥1 child failed) decides retry/replan/give-up.
4. Depth cap 2, child cap 8 per parent (config keys) — no runaway trees.

**Accept:** e2e script in results/: a parent goal fans out 2 pty-provider children +
1 local child, parent resumes with digest; unit tests for join policies.

## D3 — Advisor / supervisor mode
**Owns:** `assets/personalities/advisor/persona.json` + avatar (new bundle),
`data/skills/{daily_briefing,standup_checkin,project_review,session_digest}/skill.json`
(new), `src/scheduler/proactive_engine.cpp` advisor hooks ONLY IF needed beyond D1
(D1 owns the file first; D3 runs after D1 in-wave — serialize these two).
**Deps:** D1, C3 (screen awareness), A2.

1. **Advisor persona**: system prompt engineered for advise/supervise/manage (asks
   clarifying questions, tracks commitments via `remember`, reviews rather than does,
   direct and concise, uses `screen_describe` when asked "how's this look?", monitors
   agent sessions via `agent_status` and summarizes). Distinct Kokoro voice (after D4;
   any voice string is fine pre-D4). Tool allowlist: advisory set (recall/remember/
   web_search/fetch_page/youtube_search/screen_*/agent_*/schedule_* — NOT fs_write/
   run_command by default: the advisor advises).
2. Skills: `daily_briefing` (calendar-less v1: weather-free, memory + tasks + sessions
   + shopping digest), `standup_checkin` ("what are you working on today; want me to
   schedule/delegate anything?"), `project_review {path?}` (delegates a review to an
   external agent session and digests the result), `session_digest` (summarize all
   agent sessions + costs).
3. Seed schedules (created on first advisor activation, deletable): morning briefing
   08:00 daily; session_digest on any session completion (event-triggered via
   D1's engine subscribing to AgentSessionEvent — small hook).

**Accept:** activating Advisor persona + "give me my briefing" produces a useful
multi-source briefing; standup schedule fires and speaks; persona shows correct tool
gating (fs_write denied for advisor).

## D4 — TTS v2 (voice quality + control)
**Owns:** `src/audio/tts_piper.cpp/.h` (rename to `tts_engine.*` allowed),
`src/audio/audio_service.cpp` (init call), `tools/kokoro_worker/kokoro_worker.py`,
`src/core/config.h/.cpp` (keys), `src/ui/qml/SettingsView.qml` +
`settings_controller.*` (Voice section — wave-D turn, C1's Safety section already
landed), `src/personality/personality_manager.cpp` (voice field mapping only).
**Deps:** none within wave D (parallel-safe: files disjoint from D1/D2/D3 except
PROGRESS ticks).

1. Config keys: `tts.engine` (`auto|kokoro|piper`), `tts.voice` (default
   `af_heart` — warmer than `af_sky`; verify against installed voices-v1.0.bin),
   `tts.speed` (0.8–1.3, default 1.0), `tts.volume`.
2. Per-persona voice: persona `voice` field maps directly to a Kokoro voice id;
   `mapVoice` table covers all shipped voices (af_*/am_*/bf_*/bm_*) with graceful
   fallback + log.
3. Chunking quality: abbreviation-safe sentence splitter (Dr., e.g., 3.14, U.S.);
   merge fragments < 40 chars into the next sentence (kills choppy prosody); pass
   `speed` through; keep 280ms idle-gap protocol.
4. kokoro_worker: accept `!speed=`/`!voice=` per-utterance (exists) + a `!flush`
   no-op keepalive; try `espeak-ng` phonemizer quality flags if trivially available —
   else skip.
5. Settings ▸ Voice: engine combo, voice combo (enumerated from voices bin via a
   worker `--list-voices` mode), speed slider, "Preview" button (speaks a test line
   through the real pipeline).
6. Optional stretch (timebox 1h): evaluate `kokoro` streaming synthesis flag for
   lower first-audio latency; document findings in results/D4_tts.md.

**Accept:** voice/speed changes apply live from Settings incl. preview; two personas
speak with two distinct voices; the "robotic" check: owner listens to a paragraph at
af_heart/1.0 (subjective sign-off recorded in results/D4_tts.md).

---

# WAVE E — GUI features (all parallel, then EV verify)

## E1 — Chat: text selection + drag-scroll coexistence
**Owns:** `src/ui/qml/ChatView.qml`.
**Deps:** A1 (chat content clean).

1. Message body `Label` → read-only `TextEdit` (`readOnly:true, selectByMouse:true,
   textFormat: Text.MarkdownText` — verify markdown renders; else PlainText),
   `wrapMode` preserved, Style tokens for selection colors.
2. Gesture arbitration: mouse **drag on text selects**; wheel + scrollbar +
   touch-flick still scroll; empty-margin drag scrolls (ListView default). If
   TextEdit steals vertical flick on touch, add a `DragHandler` on the delegate
   margin / `pressDelay` tuning. Test both on desktop mouse.
3. Context menu (right-click): Copy / Select all / Copy message.

**Accept:** capture PNG sane; manual: select+copy text from an old message while the
list can still be wheel- and drag-scrolled from margins.

## E2 — Personalities editor (create / edit / destroy in GUI)
**Owns:** `src/personality/personality_manager.cpp/.h`, new
`src/ui/models/personality_model.cpp/.h`, `src/ui/qml/PersonalitiesView.qml`, new
`src/ui/qml/PersonalityEditor.qml`, `src/app/app_controller.*` (expose model).
**Deps:** none.

1. `PersonalityManager` write API: `createBundle(name)` (scaffolds persona.json from
   a template), `saveBundle(name, json)` (atomic write: tmp+rename), `deleteBundle
   (name)` (moves the folder to `personalities/.trash/<name>-<ts>` — reversible, no
   hard delete; cannot delete the active persona without switching first). The
   existing QFileSystemWatcher hot-reload picks changes up automatically.
2. `PersonalityModel` (QAbstractListModel): all fields (name, system_prompt, voice,
   preferred_model, wake_phrase, tools allowlist, sampling params, avatar_path,
   is_active).
3. PersonalitiesView: card list w/ avatar + active badge; "New" button; per-card
   Edit / Duplicate / Delete (confirm dialog); Delete of built-ins allowed but
   offers "Reset to defaults" via bundle_seed re-seed.
4. PersonalityEditor (page or large dialog): name; multiline system prompt editor
   (mono font, char count); voice combo (from D4's enumeration when present, else
   free text); model combo (fast/registry ids); wake phrase; tools multi-select
   (from ToolRegistry specs — exposed via a Q_INVOKABLE); sampling sliders
   (temp/top_p/top_k/repeat/max_tokens) with reset; avatar file picker (copies into
   bundle). Save / Cancel; dirty-state guard.
5. Stub notes → results/E2_stubs.md.

**Accept:** create → edit → activate → speak-test → delete round-trip entirely in
GUI; persona.json on disk matches editor state; capture PNGs of both views.

## E3 — Surfaces v2: research boards
**Owns:** new `src/ui/qml/surfaces/NoteSurface.qml` (markdown card), upgrade
`src/ui/qml/surfaces/ImageSurface.qml` (caption), `src/ui/qml/SurfaceHost.qml`
(groups + board layout), `src/ui/qml/surfaces/PlaceholderSurface.qml` (retire raw
argsJson dump).
**Deps:** A3 (args schema).

1. `NoteSurface` (`type:"note"`): title + markdown body (`Text.MarkdownText`),
   glass card, scrollable, max-height rules.
2. ImageSurface: optional caption bar (args.caption), fit/fill toggle, click →
   full layout.
3. **Research board**: surfaces sharing `args.group` get a labeled group frame and
   a `board` layout — group headers, notes column beside media (the owner's "clean
   bounding boxes with pictures alongside information"). `arrange {layout:"board"}`.
4. Keyboard: Esc closes focused surface; toolbar gains "Board" button.
5. Stub notes → results/E3_stubs.md.

**Accept:** scripted demo (command palette dev action): spawn 2 notes + 2 images +
1 video in 2 groups → board layout renders clean, captures saved.

## E4 — Window takeover (QML side of A3)
**Owns:** `src/ui/qml/Main.qml` (Connections + handlers), `src/app/app_controller.cpp/.h`
(window handle invokables — coordinate: E2 also edits app_controller; keep E4's edits
to a distinct, small `windowRequested` relay region agreed in results/E4_notes.md
BEFORE parallel start, or serialize E2→E4).
**Deps:** A3.

1. Handlers for `windowRequested`: `present` (raise + requestActivate + optional
   fullscreen + navigate to surfaces), `fullscreen`/`restore` (showFullScreen/
   showNormal preserving maximize state), `always_on_top`/`normal`
   (WindowStaysOnTopHint flag flip with visibility juggle), `raise`, `hide_to_tray`.
2. **Human override**: Esc exits AI fullscreen/on-top; a small "Polymath is
   presenting — Esc to dismiss" pill shows whenever the AI holds a window state;
   auto-revert after `ui.present_timeout_min` (default 30).
3. `open_page` NavigateRequest → nav rail navigation.

**Accept:** from chat: "take over the screen and show me the video" → fullscreen
present with the pill; Esc restores; open_page navigates to any nav page.

## E5 — Copy generalization + small polish
**Owns:** `src/ui/qml/ShoppingView.qml`, `src/ui/qml/ChatView.qml` empty-state
string ONLY (coordinate with E1 — same file: E5 lands after E1, tiny diff),
`src/ui/qml/PrivacyView.qml` (screen-capture toggle row for C3's key).
**Deps:** E1 (file ordering), C3 (key exists).

1. Shopping empty-state: "Add anything you need to buy — say 'add AA batteries to
   my list'." Icon stays cart.
2. Chat empty-state: refresh example prompts to showcase new powers (watch video /
   schedule / screen glance).
3. PrivacyView: `privacy.screen_capture` toggle row.

**Accept:** captures of the three views.

## EV — Wave-E stub sync + capture verify
**Owns:** `src/ui/tools/capture_views.cpp` (exclusive).
**Deps:** E1–E5, B2, D1 (all stub-notes files written).

1. Apply every `results/*_stubs.md`: StubApp invokables (personality CRUD, window
   verbs, confirm respond), context properties, new models (PersonalityModel,
   schedules in task model), seed data (populated + `--empty`), `views[]` entries
   (PersonalityEditor, VideoPickerSurface + NoteSurface via a SurfaceHost demo
   wrapper view).
2. Full capture run, all PNGs read + fixed round (the Overhaul-1 BV pattern).

**Accept:** `capture_views` green; every view/surface PNG visually sane (log in
results/EV_captures.md).

---

# WAVE F — Verify & ship

## F1 — Full builds + test suite
**Owns:** build trees, `capture_views.cpp` hotfixes, any test fixes.
**Deps:** all prior.
CPU build + CUDA build (`scripts/build-gpu.ps1`, arch 75) green; `ctest` full pass
(old 14 + all new suites); captures green on both trees.

## F2 — Live end-to-end acceptance run (GPU, voice on)
**Owns:** nothing exclusive (fix-forward commits allowed).
**Deps:** F1.
Scripted checklist (record results in results/F2_e2e.md):
1. "Open a YouTube video about <topic>" → picker → click → **ad-free playback**. ×3 topics.
2. "Slop mode" → video autoplays. No raw JSON in chat anywhere all session.
3. "Create a file on my desktop…" → confirm dialog → file exists; "delete it" →
   Recycle Bin. Denylist blocks `format` command with a chat-visible denial.
4. "Every day at 8 brief me" → schedule visible; forced fire → spoken briefing.
5. Advisor persona: standup check-in, "what's on my screen", session digest after a
   spawned pty session finishes (D2 rejoin proof).
6. Personalities: create/edit/voice-preview/delete in GUI.
7. Chat: select + copy text; drag-scroll still works.
8. "Take over the screen and show me" → present mode; Esc exits.
9. TTS: owner sign-off on voice quality (af_heart vs af_sky A/B).
10. Idle VRAM/CPU within `docs/overhaul/04_VOICE_RESOURCES.md` budget.

## F3 — Docs, graph, tag
**Owns:** docs, installer script if version-stamped.
**Deps:** F2.
Update `STATUS.md`, `WIRING.md`, `docs/MODELS.md` (if voices doc'd), project
CLAUDE.md deltas; `graphify update .`; tick PROGRESS complete; tag `v0.3.0-overhaul2`;
rebuild the Inno Setup installer (ISCC) so the desktop has a current
`Polymath-0.3.0-win64-cuda-Setup.exe`.

---

# WAVE Z — Backlog (explicitly OUT of Overhaul 2; do not start without owner ask)

- SponsorBlock segment skipping (community API) in YtClean.
- DXGI desktop duplication for low-latency screen capture; window-specific capture.
- AEC (full-duplex barge-in), custom wake word training.
- New TTS engine evaluation (only if owner unhappy post-D4): Piper high-quality
  voices, StyleTTS2-lite, cloud opt-in.
- Parallel local inference (second small model for subagents) — VRAM-gated.
- Mobile gateway/app revival; Heavy-27B idle-queue role (both parked in Overhaul 1).
- Multi-user identities tied to face recognition; per-user memory namespaces.
- Calendar integration for briefings; email/IM ingestion for the advisor.
- Undo journal for fs_write (content-addressed backups before overwrite).
- browser_drive allowlist + per-site permissions; cookie/profile management UI.
- Memory dashboard view (browse/edit semantic memories).
- Installer auto-update channel.
