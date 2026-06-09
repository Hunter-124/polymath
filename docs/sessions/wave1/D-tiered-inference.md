# Wave 1 · Card D — Tiered inference + scheduler (the headline feature)

**Read [`../SHARED.md`](../SHARED.md) first.**  Owns: **`src/inference/` + `src/scheduler/`.**
Cards A/B/E only *call* inference's API — you own the inference code. Inference bugs or new
generate options they need land in `contract-requests.md` for you.

## Goal
Prove the tiered model lifecycle + deep-work queue:
Fast resident → idle detected → load Heavy (Gemma 27B, partial offload within the VRAM budget,
evicting/▾offloading Fast) → drain the deep-task queue → unload Heavy → restore Fast.

## Verify
1. **VRAM budget** — `planGpuLayers` trims `n_gpu_layers` so Heavy fits alongside/instead of Fast
   on the budget; no OOM; eviction + restore leave Fast working.
2. **Idle-driven load** — simulate the IdleDetector going quiet → InferenceManager loads Heavy,
   the TaskScheduler drains a queued deep task, the result persists (`tasks` table), Heavy
   unloads, Fast is resident again.
3. **Streaming** — token callbacks flow Fast→EventBus during a normal generate; a deep task
   produces a result record/document.
4. **Degrade** — Heavy runs partially on CPU when it can't fully fit (slow but correct).

## How
- Read `src/inference/*` (inference_manager, llama_backend, vram_budget, grammar) and
  `src/scheduler/*` (task_queue, idle_detector, proactive_engine).
- **Use the GPU build here** — this is the one card that legitimately needs the GPU; serialize it
  against B/E's LLM checks. Drive via a small harness or the app headless; queue a deep task and
  force-idle.

## Done when
`ctest -R inference` passes (budget math + load/unload) and a logged headless run shows
Fast→Heavy→drain→Fast with no OOM. Report at `docs/sessions/reports/D-tiered.md`.
