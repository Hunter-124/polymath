# Overhaul progress ledger

Update the checkbox + add a one-line note (commit hash) as each node lands.
This file + git history = the resume state for any machine/harness.

Legend: [ ] pending · [~] in progress · [x] done · [!] blocked (see docs/overhaul/results/)

## Wave A — foundations
- [x] A0 repo/docs hygiene (MODELS.md, fetch-models.ps1, STATUS.md hardware)
- [x] A1 GUI foundation (Style, glass primitives, controls, CMake, capture flag)
- [x] A2 core foundation (event_bus, config, SettingsController, NotificationsModel, AppController, placeholder QML, stubs)
<<<<<<< HEAD
- [x] A3 harness core fixes (memory wiring, deep-task dispatch, schema, countTokens, KV-quant, 4k ctx)
- [ ] A4 audio rework (VAD-gated wake, lazy whisper, async pipeline, streaming TTS, barge-in)
=======
- [ ] A3 harness core fixes (memory wiring, deep-task dispatch, schema, countTokens, KV-quant, 4k ctx)
- [x] A4 audio rework (VAD-gated wake, lazy whisper, async pipeline, streaming TTS, barge-in)
>>>>>>> 4e9f4b0 (overhaul(A4): VAD-gated wake, lazy ASR, async pipeline, streaming TTS, barge-in)

## Wave B — GUI packages (parallel after A2)
- [x] B1 shell reskin (Main.qml)
- [x] B2 Dashboard + Chat
- [x] B3 Cameras + Timeline
- [x] B4 Tasks + Shopping
- [x] B5 Personalities + Models + Privacy + Mobile
- [x] B6 SettingsView
- [x] B7 notifications QML (ToastStack, Bell, Center)
- [x] B8 CommandPalette
- [ ] B9 SurfaceHost + surfaces
- [ ] BV wave-B build + capture verify + fix round

## Wave C — harness features
- [ ] C2 AgentLoop v2 (goals, plan/execute/reflect, context v2)
- [ ] C3 skills system + starter skills
- [ ] C4 agent sessions (service, providers, model, view)
- [ ] C5 tool registration + ui_control + browser_drive improvements
- [ ] C1 shell integration (Main.qml wiring)

## Wave D — verify & ship
- [ ] D1 full builds + captures
- [ ] D2 full test suite
- [ ] D3 models fetched + live e2e + resource audit vs budget
- [ ] D4 docs refresh + graphify update + tag v0.2.0-overhaul
- [ ] D5 (optional) QtWebEngine + real WebSurface — awaiting owner go-ahead

## Notes / deviations
- A4 landed: VAD-gated oWW, lazy whisper + idle unload, AsrWorker/TtsWorker, 16s ring, persistent streaming Piper, barge-in v1, ONNX reload backoff, device selection.
