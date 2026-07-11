# Overhaul 2 — Resume handoff (2026-07-10)

Read `00_MASTER_PLAN.md` + `01_DAG.md` first. This file is the live cursor: exactly
where execution stopped and what to do next. Delete/rewrite it as you progress.

## Committed & green (on master)
Batches 1–3 — **14 nodes** — are committed and pass a full **serial** ctest
(`ctest -j1`, 20/20). Nodes done: **A1–A4, B1–B4, D1, D4, E1–E4**.
Last committed: batch 3 `overhaul2(A4/B4/D1/E4)`. YouTube chain, harness, risk-gate,
scheduler v2, window takeover, TTS v2 are in.

## RESUME HERE — remaining DAG

### Wave C (unblocked by A4) — parallel on disjoint files
- **C1** confirmation dialog + Settings▸Safety (consumes `results/A4_contract.md`
  ConfirmRequest/Response). Owns Settings Safety section.
- **C2** system tools: fs_*/run_command/app_launch/clipboard (needs A4).
- **C3** screen_capture/screen_describe (needs A4).
→ C2/C3 both add tools to `register_tools.cpp` + `src/agent/CMakeLists.txt`
  (serialize or orchestrator-merge those two files).

### Wave D remaining
- **D2** goal-tree orchestration (needs A2,D1; edits agent_loop.cpp — serialize).
- **D3** advisor persona + skills (needs D1,C3).

### Wave E remaining
- **E5** copy generalization (needs E1,C3) — tiny.
- **EV** stub sync + capture verify (needs all E + B2 + D1 + E4 stub notes: apply
  every `results/*_stubs.md` into `src/ui/tools/capture_views.cpp`).
  **capture_views.cpp is EV-owned.**

### Wave F
- **F1** full CPU+CUDA builds + full ctest.
- **F2** live GPU e2e acceptance (10-point checklist in 01_DAG F2 — **owner
  sign-off** for YouTube demo + TTS voice).
- **F3** docs + `graphify update .` + tag `v0.3.0-overhaul2` + installer.

## Ground rules (unchanged)
One node = one owner = disjoint files; parallel subagents only across disjoint files;
orchestrator owns shared files + builds centrally + commits. Build + `ctest -j1`
green before each commit. Commits `overhaul2(<node>): …` + tick `PROGRESS.md`.
Subagents: edit only, NO git, NO build. Machine: project CLAUDE.md + C:\pm junction.

## Build recipe
```
vcvars64 (VS 18 2026) → bundled cmake --build C:/pm/build/cpu --config Release -j 6
ctest -C Release -j1 --output-on-failure   # always serial for heavy suites
```
