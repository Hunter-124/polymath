# 06 — Master DAG

The whole overhaul as an executable dependency graph. Designed for parallel subagents with
**disjoint file ownership per wave** (no two nodes in the same wave touch the same file;
shared hot-spot files are owned by exactly one node per wave, sequenced across waves).
Executable form: `.claude/workflows/polymath-overhaul.js`. Driver guide: 07_KICKOFF.md.
Tick nodes off in PROGRESS.md (+ commit) as they land.

```mermaid
graph TD
  A0[A0 repo/docs hygiene]
  A1[A1 GUI foundation: Style/glass/controls/CMake]
  A2[A2 core foundation: bus/config/settings/notifications/AppController/stubs]
  A3[A3 harness core fixes: memory wiring, deep-task dispatch, schema, KV-quant]
  A4[A4 audio rework]
  A1 --> A2
  A2 --> A3
  A2 --> A4
  A2 --> B1 & B2 & B3 & B4 & B5 & B6 & B7 & B8 & B9
  B1[B1 shell reskin Main.qml]
  B2[B2 Dashboard+Chat]  B3[B3 Cameras+Timeline]  B4[B4 Tasks+Shopping]
  B5[B5 Personalities+Models+Privacy+Mobile]
  B6[B6 SettingsView]  B7[B7 notifications QML]  B8[B8 CommandPalette]  B9[B9 SurfaceHost]
  A3 --> C2[C2 harness v2 goal loop]
  A3 --> C3[C3 skills system]
  A3 --> C4[C4 agent sessions]
  C3 --> C5[C5 tool registration + ui_control + browser_drive]
  C4 --> C5
  B1 & B2 & B3 & B4 & B5 & B6 & B7 & B8 & B9 --> BV[BV wave-B build+capture verify]
  BV --> C1[C1 shell integration Main.qml]
  C4 --> C1
  C1 & C2 & C5 & A4 --> D1[D1 full build + capture + fix pass]
  D1 --> D2[D2 full test suite]
  D2 --> D3[D3 fetch models + live run + resource audit]
  D3 --> D4[D4 docs refresh + graphify + tag]
  D3 -.-> D5[D5 optional: QtWebEngine + real WebSurface]
```

## Global rules for every node

1. **Own only your listed files.** If you believe you must edit another file, STOP and
   report — the DAG is wrong, don't improvise (no-git-merge rule: the repo history is the
   only merge mechanism).
2. **Edit agents do not run builds** when other edit nodes of the same wave are in flight;
   builds happen in the wave's verify step (or when the node is explicitly marked
   "builds"). Exception: `qmllint`-style static checks are always fine.
3. Preserve every binding/behavior listed in 01 §0/§5 and the stub-sync rule (02): any new
   QML↔C++ surface must land in capture_views stubs in the same node that creates it.
4. Commit per node: `overhaul(<node>): <summary>` + update PROGRESS.md in the same commit.
5. Default agent model: **opus**. Mechanical restyles (B3, B4, B5) may use sonnet.
   Verify/fix nodes: opus.

## Node table

### Wave A — foundations (A1→A2 sequential; A3 ∥ A4 after A2; A0 anytime)

| ID | Task / spec | Owns (files) | Acceptance |
|---|---|---|---|
| **A0** | Repo/docs hygiene. Rewrite stale `docs/MODELS.md` to the 04 §2 table; remove 27B from `scripts/fetch-models.ps1` default set (keep behind `-Heavy` flag); fix STATUS.md hardware claims (RTX 2070 Max-Q 8 GB). | docs/MODELS.md, docs/STATUS.md, scripts/fetch-models.ps1 | Docs match 04 §2; `fetch-models.ps1 -Minimal` unchanged in behavior. |
| **A1** | GUI foundation per **01 §1–3**: rewrite Style.qml; create theme/Icons.qml, effects/AuroraBackground.qml, all Glass*/Pm* new primitives; restyle the 8 existing controls (API-compatible); register everything in src/ui/CMakeLists.txt; add `pmEffectsEnabled=false` ctx prop to capture_views.cpp. **Builds + captures** (wave A allows it — no parallel edits on these files). | qml/Style.qml, qml/theme/*, qml/effects/*, qml/controls/* (all), src/ui/CMakeLists.txt, src/ui/tools/capture_views.cpp | capture_views builds; all 11 PNGs render with glass/aurora look, no tofu, no blank views; control APIs unchanged (views still load unmodified). |
| **A2** | Core foundation per **02** (+ event payloads for 03/05): event_bus.h/.cpp gains `SurfaceRequest`, `GoalUpdate`, `AgentSessionEvent` payloads+signals+publishers (ONE edit, all three); config.h/.cpp all new keys (02 table incl. agents.*, audio.*, llm.kv_quant, agent.goal_timeout_min, agent.speak_results, agents.speak_needs_input); SettingsController; NotificationsModel; AppController: register `settings`+`notifications`, vram props, wakeWordPulse, surfaceRequested+goalUpdated relays, spawnSurfaceDemo; **create+register placeholder QML files** (SettingsView, SettingsSection, ToastStack, NotificationBell, NotificationCenter, CommandPalette, SurfaceHost, surfaces/{Placeholder,Image,Web}Surface, AgentSessionsView — minimal compilable stubs) so wave B/C edits existing registered files and never touches CMake; capture_views stubs per 02. **Builds** (full). | src/core/event_bus.*, src/core/config.*, src/ui/settings_controller.*, src/ui/models/notifications_model.*, src/app/app_controller.*, src/ui/CMakeLists.txt, src/ui/tools/capture_views.cpp, all placeholder QML listed | Full build green; captures render (incl. placeholder SettingsView); `spawnSurfaceDemo` reachable; settings get/set round-trips in a unit test. |
| **A3** | Harness core fixes per **03 §1,§3** + inference per **04 §1**: ToolContext+MemoryService; semantic recall in memory tools; TaskScheduler tool/summarizer dispatch; taskFinished→delivery; history role fix; goals/plan_steps schema; InferenceManager::countTokens; n_ctx 4096 default; KV-quant q8_0 in llama_backend behind `llm.kv_quant`. | src/core/i_tool.h, src/core/schema.h, src/agent/agent_runtime.cpp(.h), src/agent/tools/memory_tools.*, src/tools task_scheduler.* (locate: src/tools or src/agent), src/app/app_controller.cpp (taskFinished wiring only), src/inference/inference_manager.*, src/inference/llama_backend.cpp, src/memory/memory_service.* (recall API surface only), tests/test_agent_e2e.cpp | Build green; deterministic tests: queued generate_lab_report produces .docx; seeded memory retrieved semantically via tool with stub embedder; taskFinished reaches NotificationsModel; migration applies on old DB. |
| **A4** | Audio rework per **04 §3** (all of it): VAD-gated wake, lazy GPU whisper + idle unload, AsrWorker/TTS worker threads, 16 s ring, persistent Piper + sentence streaming, barge-in v1, ONNX session reload, device selection from settings. | src/audio/* (all), tests/test_audio_e2e.cpp | 04 §4 acceptance: idle CPU <1 %, 0 idle whisper VRAM, barge-in works, ring never overflows, tests green. |

### Wave B — GUI packages (parallel after A2; QML-only; NO builds — BV builds)

| ID | Task / spec | Owns |
|---|---|---|
| **B1** | Shell reskin per **01 §4**: frameless window, titlebar, collapsible grouped nav rail (incl. Agents + Settings entries), PageHost transitions, aurora z-order. Feature wiring (palette/toasts/bell/surfaces/bindings) is NOT here — C1 does it; leave clearly-marked hooks. | qml/Main.qml |
| **B2** | Restyle per **01 §5.1–2** + HUD per **02 §F4** (defensive bindings). | qml/Dashboard.qml, qml/ChatView.qml |
| **B3** | Restyle per **01 §5.3,5.5**. (sonnet OK) | qml/CamerasView.qml, qml/TimelineView.qml |
| **B4** | Restyle per **01 §5.4,5.6**. (sonnet OK) | qml/TaskQueueView.qml, qml/ShoppingView.qml |
| **B5** | Restyle per **01 §5.7–10**. (sonnet OK) | qml/PersonalitiesView.qml, qml/ModelManagerView.qml, qml/PrivacyView.qml, qml/MobileAccessView.qml |
| **B6** | Implement SettingsView per **02 §F1** (placeholder → full). | qml/SettingsView.qml, qml/SettingsSection.qml |
| **B7** | Implement per **02 §F3**. | qml/ToastStack.qml, qml/NotificationBell.qml, qml/NotificationCenter.qml |
| **B8** | Implement per **02 §F2** (component only; registry lives in Main.qml → C1). | qml/CommandPalette.qml |
| **B9** | Implement per **02 §F5**. | qml/SurfaceHost.qml, qml/surfaces/* |
| **BV** | **Verify wave B**: build capture_views, run captures (both modes), read every PNG against 01 §7 checklist, fix small issues directly or report per-file defects for a fix round. Owns: build tree + may touch any wave-B file for fixes (wave-B agents are done). | — |

### Wave C — harness features (C2∥C3∥C4 after A3; C5 after C3+C4; C1 after BV+C4)

| ID | Task / spec | Owns | Acceptance |
|---|---|---|---|
| **C2** | AgentLoop v2 per **03 §2**: router, plan/execute/reflect, goal persistence/resume, delivery via GoalUpdate, context assembly v2 (token-budgeted, memory-injected, rolling summary). | src/agent/agent_loop.* (new), src/agent/agent_runtime.* , src/agent/turn_collector.*, src/agent/persona.*, tests/test_harness_e2e.cpp (new) | 03 §7 deterministic tests green; model-gated tests skip-green. |
| **C3** | Skills per **03 §4**: SkillRegistry + watcher, run_skill/save_skill tool *classes* (registration → C5), 3 starter skills. | src/agent/skills/* (new), src/agent/tools/skill_tools.* (new), data/skills/* | Registry loads/validates/substitutes params; starter skills expand to correct steps (unit-tested). |
| **C4** | Agent sessions per **05**: pm_sessions lib (service, providers claude-code/codex/pty), SessionsModel, AgentSessionsView full impl, AppController integration (instantiate+register `agentSessions`), capture stub for agentSessions, session tables schema. | src/sessions/* (new), src/sessions/CMakeLists.txt, top-level src/CMakeLists.txt (add subdir), src/app/CMakeLists.txt (link), src/app/app_controller.* (sessions instantiation only), src/ui/models/sessions_model.*, qml/AgentSessionsView.qml, src/ui/tools/capture_views.cpp (stub add), tests/test_sessions_e2e.cpp (new) | 05 §6 deterministic tests green (fixture parsing, allowlist, model states); live claude test skip-green. |
| **C5** | Register all new tools (run_skill, save_skill, agent_*, ui_control) per 02 §F5 + 03 + 05; tool risk classes per **03 §5**; browser_drive improvements (persistent session reuse, screenshot capture into tool result). **Builds.** | src/agent/tools/register_tools.cpp, src/agent/tools/ui_control_tool.* (new), src/agent/tools/browser_drive.*, src/agent/tool_registry.* (risk metadata) | Build green; test_agent_e2e tool-inventory test updated & green; ui_control spawn round-trip test (bus → relay signal). |
| **C1** | Shell integration per **02 execution-order step 3**: Main.qml gets palette registry + Ctrl+K, ToastStack swap, bell+center, SurfaceHost overlay, Style↔settings Bindings, nav entries live. **Builds + captures.** | qml/Main.qml | Captures: shell PNG shows titlebar bell + palette pill; palette opens (capture via stub interaction if feasible, else live check in D3); no regressions in other views. |

### Wave D — integrate, verify, ship

| ID | Task | Acceptance |
|---|---|---|
| **D1** | Full build of both trees (build/cpu VS + build/cuda2 Ninja CUDA per memory: use scripts/build-gpu.ps1 path conventions), fix all compile/link fallout. Full capture run. | Both builds green; captures clean. |
| **D2** | `ctest` full suite in build/cpu (+ new suites); fix failures. | 11 legacy + 2 new suites green. |
| **D3** | `scripts/fetch-models.ps1` (base set per 04 §2, ~8 GB download); live run `Polymath.exe`: voice loop (wake→ask→answer→TTS), barge-in, Ctrl+K, settings persistence, spawnSurfaceDemo, agent_spawn against a real `claude -p` session, goal end-to-end ("research X and summarize"), **resource audit vs 04 §1** (nvidia-smi VRAM table + idle CPU screenshot into docs/overhaul/results/). | Budget table holds (±10 %); all live checks pass; results committed. |
| **D4** | Docs refresh: README (new features), ARCHITECTURE.md (harness v2, sessions, surfaces), STATUS.md; `graphify update .`; PROGRESS.md complete; tag `v0.2.0-overhaul`. | Docs match reality; tag pushed to any remote if configured. |
| **D5** *(optional, needs owner decision — ~1 GB Qt module install)* | Install QtWebEngine for Qt 6.6.3 (`aqt install-qt windows desktop 6.6.3 win64_msvc2019_64 -m qtwebengine qtwebchannel qtpositioning` into C:\Qt), link Qt6::WebEngineQuick, implement real WebSurface (WebEngineView + UrlRequestInterceptor adblock + YouTube clean-mode script), windeployqt updates. | YouTube plays in a surface with adblock; slop_mode skill fully live. |

## Failure protocol

A node that cannot meet acceptance: commit nothing, write findings to
`docs/overhaul/results/<node>-blocked.md`, mark PROGRESS.md ⚠, continue independent nodes.
The driver resolves blockers before dependent nodes start.
