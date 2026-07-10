# Polymath — Claude Code project notes

Local, privacy-first AI home assistant (C++/Qt6/QML + llama.cpp/whisper/ONNX). Vision:
Jarvis / Star Trek computer. **The 2026-07 overhaul plan in `docs/overhaul/` is the source
of truth for all in-flight work** — read `docs/overhaul/00_MASTER_PLAN.md` first, execution
state in `docs/overhaul/PROGRESS.md`.

## Machine truth
i7-9750H (6C/12T), 32 GB RAM, RTX 2070 Max-Q **8 GB VRAM**, Windows 10. Resource budget:
`docs/overhaul/04_VOICE_RESOURCES.md` §1. (Older docs claiming a 3080 Ti are wrong.)
`hearth/` at the workspace root is a symlink alias of this repo — one tree, no fork.

## Build / verify (fast loop)
- UI-only iteration (no CUDA, minutes not tens of minutes):
  `cmake --build build/cpu --config Release --target capture_views`
  then `$env:QT_QPA_PLATFORM='offscreen'; build/cpu/bin/Release/capture_views.exe <outdir> [--empty]`
  → renders every QML view to PNG with stub data. Read the PNGs to verify visuals.
- Full builds: `docs/BUILD.md`; GPU tree via `scripts/build-gpu.ps1` (portable CUDA,
  no-space `C:\pm` junction). Tests: `ctest` in the build tree.
- Any new QML↔C++ surface must be mirrored in the stubs in `src/ui/tools/capture_views.cpp`
  or headless captures crash.

## Conventions
- QML theme: `Style.qml` singleton tokens only — no hardcoded colors/sizes in views.
  Design system spec: `docs/overhaul/01_GUI_DESIGN_SYSTEM.md`.
- Cross-thread: services on QThreads talk ONLY via `EventBus` (queued value-struct
  payloads). Tools never touch QObjects directly.
- Overhaul commits: `overhaul(<node>): <summary>` + tick `docs/overhaul/PROGRESS.md` in the
  same commit. One DAG node = one owner = disjoint files (see `docs/overhaul/06_DAG.md`
  global rules).
- Repo-local git identity is set (`Polymath <local@polymath>`); no remote is configured.
