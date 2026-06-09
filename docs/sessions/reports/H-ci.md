# Card H — Integration test harness + CI — report

**Branch:** `wave2/H-integration-ci`  ·  **Owns:** `tests/` (own additions) + `scripts/ci.ps1`
**Result:** PASS. Full `ctest` green from a clean build; `scripts/ci.ps1` reproduces it.

---

## Verified

### Full suite green (clean CPU build)
`pwsh scripts/build-cpu.ps1` builds clean and `ctest --test-dir build/cpu -C Release`
passes **9/9** with the models tree present (this dev box junctions
`build/cpu/bin/Release/data` to the shared copy):

```
1/9 core .......... Passed     2/9 tools ......... Passed
3/9 audio ......... Passed     4/9 agent ......... Passed
5/9 vision ........ Passed     6/9 inference ..... Passed
7/9 memory ........ Passed     8/9 privacy ....... Passed
9/9 integration ... Passed              100% tests passed, 0 failed out of 9
```

### `scripts/ci.ps1` — both runner profiles
* `pwsh scripts/ci.ps1 -Clean` on a box **without** models (simulating CI: a clean
  build wipes `build/cpu` and with it the junctioned `data/`, so models are absent)
  → clean configure+build, then **8/8** green (the one model-hard test, `memory`,
  is auto-excluded with a warning), `exit 0`.
* `pwsh scripts/ci.ps1` with models present → **full 9/9** incl. `memory`, `exit 0`.
* On red (build or any test) the script exits non-zero — verified (the pre-fix
  clean run surfaced `memory`'s failure as `CI_EXIT=8`).

### New headless drive-the-app harness — `tests/test_harness.h`
A header-only, reusable fixture (`polymath::test::HeadlessApp`) that stands up the
**real** backend offscreen exactly as the app does — `Paths::setRoot(temp)` then
`AppController::initialize()` — bringing up all **8 services on their own QThreads**
wired through the real `EventBus`, **without the GUI / QML engine**. It points
`Paths` at a throwaway temp root, so every model-/hardware-dependent service
degrades cleanly (InferenceManager: "no Fast model"; AudioService: no mic models;
Vision: no ONNX) and the cross-service *plumbing* is exercised without a 28 GB
dependency. Provides:
* `boot()` — initialize + a second `Database` handle on the same on-disk file (WAL
  → concurrent readers) so a test asserts what a *different* service persisted
  through `AppController`'s private `db_`;
* `BusCapture<Payload>` — records any EventBus signal's payloads on the harness
  thread for assertions across the thread hop;
* `pump(ms)` / `waitFor(pred, ms)` — spin the Qt event loop so queued cross-thread
  signals actually deliver;
* `injectCommand(text)` — publish a post-wakeword `Utterance` as AudioService would.

### New cross-service integration test — `tests/test_integration_e2e.cpp` (`ctest` name: `integration`)
Boots the whole backend via the harness and asserts flows that **span module
boundaries** over the real bus + DB. All deterministic & model-free in the default
suite (so CI stays green without models):

1. **`Utterance → AgentRuntime → transcripts(DB) + SpeakRequest(bus)`** — the card's
   marquee flow. A command utterance is published on the bus; `AppController` has
   wired `EventBus::utterance → AgentRuntime::handleUtterance`, so the agent worker
   runs a full turn on its own thread. With no model, `InferenceManager::generate()`
   returns `"[no model loaded]"` (done=true) immediately, so the agent loop still
   (a) persists the assistant transcript and (b) publishes a `SpeakRequest` for TTS.
   The test asserts **both** the bus hop (SpeakRequest captured on the UI thread)
   and the DB side effect (transcript row) — proving audio→agent→DB→audio is live
   across three service threads.
2. **`setPrivacy() → privacyChanged(bus) + settings(DB)`** — the system-wide kill
   switch contract: the action announces on the bus *and* persists; asserted via
   both the controller's read-back and the independent DB handle.
3. **`addShoppingItem() → shopping_items(DB)`** — a QML-callable action routes
   through the UI model to the store; the second DB handle confirms the rows.
4. **`queue_deep_task → tasks(DB,'queued') + taskUpdated(bus)`** — the queue half of
   the *deep task → scheduler → Heavy model → result* flow, always run. Standalone
   `TaskScheduler`+`InferenceManager` on their own threads (so the test owns the
   idle trigger). The **drain half** (idle → load Heavy → run → terminal status) is
   **opt-in** behind `POLYMATH_E2E_FULL=1` so the default suite never depends on a
   heavy load.

`integration` ran in ~1.1 s and is registered with `TIMEOUT 300` and
`ENVIRONMENT QT_QPA_PLATFORM=offscreen`.

---

## Changed (files + why)

| File | Change | Why |
|------|--------|-----|
| `tests/test_harness.h` | **new** | Reusable headless drive-the-app fixture (the card's "backbone"). |
| `tests/test_integration_e2e.cpp` | **new** | Cross-service integration flows over the real bus/DB/threads. |
| `tests/CMakeLists.txt` | **append-only add** | Register the `integration` target (links `pm_app`; offscreen env; 300 s timeout). Existing per-module block untouched & all still pass. |
| `scripts/ci.ps1` | **new** | CI entry: clean `build-cpu` + full `ctest`, non-zero on red; documents CPU-only / no-models constraints and auto-excludes the one model-hard test when models are absent. |

No `src/` modules, GUI, or frozen contracts (`event_bus.h/.cpp`, `schema.h`) were
touched. The integration flows use only existing EventBus signals and schema —
**no contract change required**, so nothing was appended to
`docs/sessions/contract-requests.md`.

---

## Real issue surfaced (routed to an owner)

**`test_memory` (Wave-1 Card E) hard-requires the EmbeddingGemma model — not env-gated.**
`tests/test_memory_e2e.cpp:158` does
`assert(embeddingModelOnDisk(dataModels) && "EmbeddingGemma .gguf not found …")`.
On a machine without the ~28 GB models (i.e. a real CI runner, or any clean build
that hasn't re-junctioned `data/`), this **hard-fails** (`0xC0000409`) rather than
skipping. It is the **only** per-module test that hard-depends on a model on disk
(by contrast, `inference` is gated on `POLYMATH_E2E_FULL`, and `audio`/`vision`
degrade cleanly when their models are absent).

* **Owner:** Card E (memory). I do not own that test and did not modify it.
* **Routed action for Card E:** gate the vector-recall steps behind the existing
  opt-in convention — skip-with-message when `embeddingModelOnDisk()` is false
  (and/or `POLYMATH_E2E_FULL`/`POLYMATH_E2E_EMBED` unset), matching how
  `test_inference_e2e` handles its heavy half — so the test is green-by-skip on a
  bare runner and runs fully where the model exists.
* **Stopgap I shipped (so CI is green now, no src/Card-E edits):** `ci.ps1` probes
  for `data/models/embeddings/*.gguf`; if absent it runs `ctest -E '^memory$'` and
  prints a warning. When models are present it runs the full suite including
  `memory`. This is documented inline in `ci.ps1`'s header and removable once Card
  E adds the proper gate.

---

## Residual gaps

1. **`memory` test gating** — above. Stopgap in `ci.ps1`; real fix belongs to Card E.
2. **LLM-in-the-loop cross-service flow stays deterministic-by-design.** The
   integration test deliberately drives the agent with *no* model so the flow is
   fast & CI-safe; the *content* of the answer is the no-model fallback. A true
   model-backed `Utterance → tool-call → DB` assertion already exists, opt-in, in
   `test_agent` (`POLYMATH_E2E_LLM=1`) — and that path notes the gemma-3n
   constrained-decode fast-fail in `src/inference` (out of scope here). The
   integration suite intentionally does not re-litigate that; it proves the
   *wiring*, not the model.
3. **No GitHub Actions workflow committed** (the card marks it optional). `ci.ps1`
   is the portable entry point and documents the runner constraints (CPU-only, no
   GPU runner, models absent → heavy/embedding tests gated/excluded). A workflow,
   if added later, is a thin `pwsh scripts/ci.ps1 -Clean` step on a
   `windows-latest` runner with the Qt/OpenCV/ONNX prereqs cached.

---

## How to run

```powershell
# CI default (what a bare runner does): clean build + full ctest, memory auto-skipped if no models
pwsh scripts/ci.ps1 -Clean

# Local with models present: full 9/9 suite
pwsh scripts/ci.ps1

# Just the new integration test:
ctest --test-dir build/cpu -C Release -R integration --output-on-failure

# Exercise the heavy/model-backed opt-in halves (needs models on disk):
$env:POLYMATH_E2E_FULL=1   # scheduler Heavy drain in test_integration / test_inference
$env:POLYMATH_E2E_LLM=1    # live Fast-model tool turn in test_agent
pwsh scripts/ci.ps1 -Full
```
