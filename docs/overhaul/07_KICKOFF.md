# 07 — Kickoff: how to drive this overhaul

For the driver session (Claude Opus in Claude Code, or any capable harness) — on this
machine or a fresh clone.

## Prerequisites (this machine already satisfies all of them)
- Windows, VS 2026 toolchain, Qt 6.6.3 at `C:\Qt\6.6.3\msvc2019_64`, configured build tree
  at `polymath/build/cpu` (VS generator; `capture_views` target is the fast UI loop).
  Fresh machine: follow `docs/BUILD.md` + `scripts/setup-dev.ps1` first.
- No model weights needed until node D3 (`scripts/fetch-models.ps1`).
- Optional but recommended: `graphify` CLI (knowledge graph at `graphify-out/`; use
  `graphify query "<question>"` for codebase questions before grepping).

## Drive it

1. Read `docs/overhaul/00_MASTER_PLAN.md`, then `06_DAG.md`, then `PROGRESS.md` to see
   what's already landed (git log `overhaul(` commits are the same ledger).
2. Execute wave by wave with the checked-in workflow:
   ```
   Workflow { scriptPath: ".claude/workflows/polymath-overhaul.js", args: { wave: "A" } }
   ```
   …then `"B"`, `"C"`, `"D"`. Between waves: skim the node reports, resolve any `[!]`
   blockers (results/*-blocked.md) yourself before starting dependent nodes. `{wave:"all"}`
   runs everything but wave boundaries are the natural checkpoints — prefer per-wave.
3. If a wave is interrupted mid-run, re-running that wave is safe **only for nodes that
   didn't commit** — check `git log --oneline | grep overhaul` and PROGRESS.md, and edit the
   workflow args or run the missing nodes as individual agents with the same prompts.
4. Subagent policy: default opus (inherit); B3/B4/B5 may run sonnet. One node = one agent =
   one commit. Never let two concurrent agents own the same file — the DAG guarantees this
   if you respect node boundaries.
5. After any wave that touched C++/QML: the wave's verify node already rebuilt + captured;
   spot-check the PNGs yourself (`build/cpu/bin/Release/_shots/`).

## Resume from another machine
Clone the repo (the spec pack + PROGRESS.md + workflow script travel with it), restore
prerequisites, `git submodule update --init` (llama.cpp/whisper.cpp/etc. are gitignored
checkouts), reconfigure `build/cpu`, continue at the first unticked PROGRESS.md node.

## Owner decision still open
- **D5 (QtWebEngine)**: ~1 GB Qt module install to make WebSurface/YouTube real. Ask the
  owner before running it. Everything else ships with the graceful WebSurface placeholder.
