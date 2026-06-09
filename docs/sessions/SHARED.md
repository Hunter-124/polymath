# Shared brief — every card in every wave reads this first

You are one agent in a parallel effort hardening **Polymath**, a local C++/Qt6 AI home
assistant (this checkout, git branch `master`). The scaffold + GPU bring-up made it compile,
link, boot, and run GPU inference. **Your job is not greenfield** — the code for your area
already exists. Your job: **drive it, find what's actually broken, fix it, and prove it works
with a test.** "Compiles and initializes" is the starting line, not the goal.

## Read before changing anything
- `docs/STATUS.md`, `docs/ARCHITECTURE.md`, `docs/PLAN.md` — what exists and what's verified.
- Your area's own headers + impl (named in your card). Read the code first.
- Your wave's `README.md` and your card.

## The two FROZEN contracts — DO NOT EDIT
- `src/core/event_bus.h` (+ `.cpp`) — every cross-service message. Services talk ONLY via the
  EventBus, never by direct cross-thread calls.
- `src/core/schema.h` (`kSchemaSQL`) — the SQLite schema.

Need a new EventBus signal or schema column? **STOP.** Append the proposal (rationale +
the workaround you used meanwhile) to `docs/sessions/contract-requests.md` and code around it.
A coordinator reconciles all requests in one pass at the end of the wave. Other agents depend on
the contract's current shape — never change it unilaterally.

## Build & run (CPU build — no GPU, avoids contention)
```powershell
pwsh scripts/build-cpu.ps1     # -> build/cpu/bin/Release/Polymath.exe (incremental)
ctest --test-dir build/cpu -C Release
```
Models (~28 GB, gitignored) live in `build/cpu/bin/Release/data/models`. In a fresh worktree,
DON'T re-download — junction to the main checkout:
```powershell
New-Item -ItemType Junction -Path <worktree>\build\cpu\bin\Release\data `
         -Target <main-checkout>\build\cpu\bin\Release\data
```
Run headless: `$env:QT_QPA_PLATFORM='offscreen'; .\Polymath.exe` (from the bin dir). Logs at
`data/logs/polymath.log` (flush every 3 s). Prefer a small focused test exe over the whole app.

## Rules of engagement
- **Own only the directory your card names.** Don't touch other `src/` modules, the GUI
  (`src/ui`, unless that's your card), or the frozen contracts.
- Work on branch `<wave>/<card-id>` in your own git worktree.
- Use **recorded/synthetic inputs** (a WAV, a video clip, seeded DB rows). Never assume a live
  mic/camera/display, and don't hold the GPU — agents share one machine. Anything needing live
  hardware serializes; design verification to avoid it where you can.
- Commit as `git -c user.email=local@polymath -c user.name=Polymath commit`, ending the message
  with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

## Definition of done (all cards)
1. The happy path **provably works** — the verification in your card passes.
2. **≥1 integration test** added: a new `tests/test_<area>_e2e.cpp` registered in
   `tests/CMakeLists.txt` (append-only block — trivial to merge), green under `ctest`.
3. A report at `docs/sessions/reports/<card-id>.md`: what you verified, what was broken, what
   you changed (files + why), residual gaps, and any contract requests.
4. You didn't break the build: `build-cpu.ps1` stays green and `ctest` passes.
