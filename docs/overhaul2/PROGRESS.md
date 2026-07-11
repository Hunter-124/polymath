# Overhaul 2 — progress ledger

Tick the checkbox + add a one-line note (commit hash) as each node lands, in the same
commit as the node. This file + `git log --grep=overhaul2` = the resume state for any
machine/harness. Legend: [ ] pending · [~] in progress · [x] done · [!] blocked
(see docs/overhaul2/results/).

> **RESUME POINTER (2026-07-10):** Batches 1–4 (A1–A4, B1–B4, C1–C3, D1, D4, E1–E4)
> committed + green (serial ctest 21/21). Next: D2/D3, E5, EV, F.

## Wave A — harness correctness
- [x] A1 final-answer hygiene + router v2
- [x] A2 goal execution integrity
- [x] A3 ui_control schema v2
- [x] A4 risk-gate enforcement (SafetyPolicy + waiting_user + audit)

## Wave B — YouTube pipeline
- [x] B1 youtube_search tool
- [x] B2 video surface v2 + VideoPickerSurface
- [x] B3 adblock + clean-mode hardening
- [x] B4 watch_video skill + step-result chaining

## Wave C — computer use + safeguards
- [x] C1 confirmation UX + Settings ▸ Safety
- [x] C2 system tools (fs_*, run_command, app_launch, clipboard)
- [x] C3 screen_capture + screen_describe

## Wave D — harness expansion
- [x] D1 scheduler v2
- [ ] D2 goal-tree orchestration
- [ ] D3 advisor/supervisor persona + skills
- [x] D4 TTS v2

## Wave E — GUI features
- [x] E1 chat text selection + drag-scroll
- [x] E2 personalities editor
- [x] E3 surfaces v2
- [x] E4 window takeover handlers
- [ ] E5 copy generalization
- [ ] EV stub sync + full capture verify

## Wave F — verify & ship
- [ ] F1 full builds + ctest
- [ ] F2 live e2e acceptance (owner sign-off YouTube + TTS)
- [ ] F3 docs + graphify + tag v0.3.0-overhaul2

## Notes / deviations
- Batch 3: A4/B4/D1/E4 — see prior notes; serial ctest 20/20.
- Batch 4 (C1/C2/C3): ConfirmDialog + Safety settings + tool_overrides; 9 system
  tools + Recycle Bin delete; screen_capture/describe with privacy.screen_capture;
  tool count 40; test_system_tools; serial ctest 21/21.
