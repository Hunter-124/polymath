# Wave 2 · Card H — Integration test harness + CI

**Read [`../SHARED.md`](../SHARED.md) first.**  Owns: **`tests/` + a CI entry under `scripts/`.**

## Goal
Lift testing from 2 unit tests (`test_core`, `test_tools`) to a real integration suite + CI.

## Do
1. **Headless drive-the-app harness** — a fixture that boots the services offscreen and injects
   EventBus messages / asserts outputs (stand up `AppController` without the GUI). This is the
   reusable backbone the cross-service tests build on.
2. **Integration tests** — wire the wave-1 cards' E2E tests into one `ctest` suite and add
   cross-service flows that span modules, e.g. `Utterance → AgentRuntime → tool → DB →
   SpeakRequest`, and `deep task → scheduler → Heavy model → result`.
3. **CI script** — `scripts/ci.ps1`: clean `build-cpu` + `ctest`, non-zero exit on red; document
   it. A GitHub Actions workflow is optional — note the GPU/runner constraints (CI runs the CPU
   build only).

## How
- Read `tests/CMakeLists.txt`, `test_core.cpp`, `test_tools.cpp` for the existing test style, and
  `src/app/app_controller.*` for how services are constructed/run.

## Done when
`ctest` runs the full integration suite green from a clean build, and `scripts/ci.ps1` reproduces
it. Report at `docs/sessions/reports/H-ci.md`.
