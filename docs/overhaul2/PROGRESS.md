# Overhaul 2 — progress ledger

Tick the checkbox + add a one-line note (commit hash) as each node lands, in the same
commit as the node. This file + `git log --grep=overhaul2` = the resume state for any
machine/harness. Legend: [ ] pending · [~] in progress · [x] done · [!] blocked
(see docs/overhaul2/results/).

## Wave A — harness correctness
- [ ] A1 final-answer hygiene + router v2 (kills tool-call leak; "open a youtube video" routes)
- [ ] A2 goal execution integrity (run_skill executes, resumeActiveGoals, session rejoin)
- [ ] A3 ui_control schema v2 (open_page, window verbs, surface args)
- [ ] A4 risk-gate enforcement (SafetyPolicy core + waiting_user + audit)

## Wave B — YouTube pipeline (top priority)
- [ ] B1 youtube_search tool (Innertube, no API key)
- [ ] B2 video surface v2 + VideoPickerSurface
- [ ] B3 adblock + clean-mode hardening (YtClean.js, interceptor)
- [ ] B4 watch_video skill + slop_mode upgrade + live e2e

## Wave C — computer use + safeguards
- [ ] C1 confirmation UX + Settings ▸ Safety
- [ ] C2 system tools (fs_*, run_command, app_launch, clipboard)
- [ ] C3 screen_capture + screen_describe

## Wave D — harness expansion
- [ ] D1 scheduler v2 (timed/recurring agent goals + tools + Tasks UI)
- [ ] D2 goal-tree orchestration (local subagents, join policies)
- [ ] D3 advisor/supervisor persona + skills + seed schedules
- [ ] D4 TTS v2 (engine/voice/speed config + UI, per-persona voices, chunking)

## Wave E — GUI features
- [ ] E1 chat text selection + drag-scroll coexistence
- [ ] E2 personalities editor (create/edit/destroy in GUI)
- [ ] E3 surfaces v2 (NoteSurface, captions, research board)
- [ ] E4 window takeover handlers (present/fullscreen/on-top + Esc override)
- [ ] E5 copy generalization (shopping hint, chat examples, privacy row)
- [ ] EV stub sync + full capture verify

## Wave F — verify & ship
- [ ] F1 full builds (CPU + CUDA) + full ctest
- [ ] F2 live e2e acceptance checklist (results/F2_e2e.md)
- [ ] F3 docs + graphify + tag v0.3.0-overhaul2 + installer

## Notes / deviations
- (add as they happen)
