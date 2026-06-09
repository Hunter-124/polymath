# Card D — Tiered inference + scheduler — report

**Status: PASS.** Both builds green; `ctest -R inference` green on CPU and CUDA; the
headline Fast→idle→Heavy→drain→Fast cycle ran headless on the RTX 3080 Ti with the
real Gemma 3 27B Heavy model and **no OOM**; the gemma-3n grammar fast-fail (card B's
`0xC0000409`) is fixed and proven with a real constrained decode.

Owned dirs: `src/inference/`, `src/scheduler/`. No frozen contracts edited. No
contract requests (none needed).

---

## Verified

### ctest (`ctest -R inference`)
- **CUDA build:** `3/3` pass — `inference` 7.33 s (includes a GPU grammar-constrained
  decode on gemma-3n). Full suite `core` + `tools` + `inference` all green.
- **CPU build:** `3/3` pass — `inference` 8.86 s (CPU grammar decode + load/unload).

The single registered test is `tests/test_inference_e2e.cpp` (ctest name `inference`),
tiered so the default suite stays fast/green while the heavy GPU steps are opt-in:
1. `[budget]` — `planGpuLayers` full-fit / partial-offload / eviction+restore /
   no-headroom→CPU; `estimateModelMiB`. Pure math, always runs.
2. `[grammar]` — `buildToolCallGrammar` emits valid GBNF **and** a real
   grammar-CONSTRAINED decode runs through `LlamaBackend` on the Fast gemma-3n GGUF
   (the exact crash repro). Skippable via `POLYMATH_SKIP_GRAMMAR_DECODE`.
3. `[lifecycle]` — `InferenceManager` Fast resident → `requestHeavy` load/unload of
   the real Heavy 27B. The heavy swap is gated behind `POLYMATH_E2E_FULL`.
4. `[drain]` — the full Fast→idle→Heavy→drain a queued deep task→result persists→Fast,
   on real service `QThread`s. Gated behind `POLYMATH_E2E_FULL` (slow; uses the 27B).

### Headline cycle (headless, `QT_QPA_PLATFORM=offscreen`, RTX 3080 Ti)
Run with `POLYMATH_E2E_GPU=1 POLYMATH_E2E_FULL=1`. Key `polymath.log` lines
(drain tier — Fast→Heavy→drain→Fast, **no OOM**):

```
TaskScheduler: enqueued task 1 type=summary priority=5
TaskScheduler: idle — draining 1 queued task(s)
InferenceManager: loaded 'gemma-3n-E4B-it-Q4_K_M' as fast (~5352 MiB, 36 layers)
InferenceManager: Fast model resident
requestHeavy(on): evicting resident set for 'gemma-3-27b-it-Q4_K_M'
InferenceManager: unloaded fast ('gemma-3n-E4B-it-Q4_K_M')
VramBudget: partial offload 25/63 layers (avail 7424 MiB, model ~18119 MiB)
LlamaBackend loaded 'gemma-3-27b-it-Q4_K_M' role=1 n_ctx=8192 ngl=25 (~16804 MiB)
InferenceManager: loaded 'gemma-3-27b-it-Q4_K_M' as heavy (~16804 MiB, 25 layers)
TaskScheduler: running task 1 (summary)
TaskScheduler: task 1 finished status=done (236 chars)
requestHeavy(off): unloading Heavy, restoring Fast
InferenceManager: unloaded heavy ('gemma-3-27b-it-Q4_K_M')
InferenceManager: loaded 'gemma-3n-E4B-it-Q4_K_M' as fast (~5352 MiB, 36 layers)
TaskScheduler: queue drained / paused (idle=true)
```

Test stdout:
```
[lifecycle] Heavy swap exercised
[drain] task 1 status=done result_chars=293
[drain] OK — task drained on Heavy, Fast restored, no OOM
test_inference_e2e: OK
```

This covers all four card requirements:
1. **VRAM budget** — `planGpuLayers` trimmed Heavy to **25/62 layers** so it fits the
   live ~7.4 GB budget (degrade path); Fast evicted then restored; no OOM.
2. **Idle-driven load** — forced idle → Heavy loaded → queued `summary` task drained
   → result persisted (`tasks.status='done'`, 293-char `result_json`) → Heavy
   unloaded → Fast resident again.
3. **Streaming** — `StreamCollector` collected the streamed tokens (Fast→EventBus
   path) into the task result; a `tasks` record was produced.
4. **Degrade** — Heavy ran with a partial GPU offload (25/62 layers, rest on CPU) and
   produced a correct result, just slower.

### Grammar crash (card B's `0xC0000409`) — VERDICT: **our bug, fixed.**
Root cause was **not** the gemma3n architecture and **not** the grammar binding — it
was our sampler driving. `LlamaBackend::buildSampler` spliced the grammar sampler as
the *first* stage of the main chain, over the full vocab, then let `dist` pick. On
some distributions that hands back a token the grammar does not actually accept (e.g.
an EOG token while the grammar is not in an accepting state, or a token whose only
surviving probability mass came from a candidate the grammar rejects). Accepting that
token empties the grammar stacks, and the **next** `llama_grammar_apply_impl` hits
`GGML_ASSERT(!stacks.empty())` / `GGML_ABORT("fatal error")` in
`third_party/llama.cpp/src/llama-grammar.cpp` — which on Windows surfaces as the
`0xC0000409` fast-fail card B saw on the first constrained decode.

Fix: a `Sampler` class (in `llama_backend.cpp`) that keeps the grammar sampler
*separate* from the main chain and uses the grammar-checked-resample loop from
llama.cpp's own `common_sampler` — sample from the main chain, verify the candidate
against the grammar in isolation, and if rejected re-apply the grammar over the full
distribution and pick the best surviving token. The accepted token is therefore always
grammar-legal, so the stacks never empty unexpectedly. We also break *before* feeding
an EOG token to the grammar.

**Proof:** a real grammar-constrained decode on **gemma-3n-E4B** (the exact crash
model) now completes and emits valid tool-call JSON instead of fast-failing:
```
[grammar] constrained decode produced 87 chars:
  {"tool":"add_reminder","arguments":{"text":"Take vitamins","when":"9:00 AM tomorrow"}}
[grammar] constrained decode OK — no 0xC0000409 crash
```
No other fast GGUF is needed — gemma-3n works with the grammar path now. (`llama-bench`
also confirmed the engine itself decodes both models fine, isolating the bug to our
sampler.)

---

## Broken → fixed (files + why)

1. **`src/inference/llama_backend.cpp` — grammar sampler ordering (the `0xC0000409`).**
   Replaced `buildSampler` (grammar first in the main chain) with a `Sampler` class
   that samples from the main chain then verifies/re-samples against a *separate*
   grammar sampler. `generate()` and `describeImage()` use it. This is the card-B fix.

2. **`src/inference/inference_manager.cpp` — `requestHeavy` ran model loads on the
   caller's thread → cross-thread CUDA fault.** A llama/CUDA context is thread-affine.
   The scheduler thread called `requestHeavy(true)` directly, creating the Heavy
   context on the *scheduler* thread, but `runGenerate()` decoded it on the
   *inference* thread → `0xC0000005` mid-decode. Fixed by marshalling `requestHeavy`
   onto the InferenceManager's own thread (`BlockingQueuedConnection`) so every touch
   of a backend/CUDA context happens on one thread. **This was a latent production bug**
   — `AppController` runs Inference and Scheduler on separate threads, so the app's
   first real idle-driven deep-work drain would have hit it.

3. **`src/inference/inference_manager.cpp` — `loadModel` budgeted against a fake layer
   count.** `planGpuLayers` was called with `n_layers_total = 999` (the registry cap),
   so "partial offload 409/999" on a 62-layer model meant *all* layers on GPU —
   over-committing the 12 GB card (host spill at ~0.4 t/s, and feeding the cross-thread
   fault). Added `LlamaBackend::probeLayerCount()` (reads `*.block_count` from the gguf
   header, no weights) and plan against the **real** layer count → Heavy now correctly
   trims to 25/62 layers and fits the budget.

4. **`src/inference/inference_manager.cpp` — deep tasks routed to Fast, not Heavy.**
   `runGenerate` with an empty `model_id` always did `ensureLoaded(Fast)`. During a
   Heavy drain that loaded Fast *on top of* the resident Heavy (no headroom → ngl=0).
   Now: when `heavy_loaded_`, a model_id-less request routes to the resident Heavy
   (the deep-work contract), with Fast as a safety net.

5. **`src/scheduler/task_scheduler.{h,cpp}` — `~TaskScheduler` was implicit.** It holds
   `unique_ptr<StreamCollector>` (forward-declared), so an implicit destructor at an
   external call site (the test, `AppController`) failed to compile ("delete of
   incomplete type"). Declared the destructor and defined it `=default` in the `.cpp`.

6. **`src/inference/CMakeLists.txt` — added a PUBLIC `POLYMATH_INFERENCE_HAS_LLAMA`
   marker** so downstream consumers (the e2e test) can tell the real engine is linked
   without inheriting llama's private headers/flags. `POLYMATH_HAVE_LLAMA` stays
   PRIVATE.

## Added
- **`tests/test_inference_e2e.cpp`** + registration in `tests/CMakeLists.txt`
  (append-only block, ctest name `inference`).

---

## Residual gaps
- **Heavy at ~0.4 t/s on a 12 GB card.** With only ~25/62 layers on GPU the 27B is
  correct but slow; a `summary` task took ~44 s. That is the intended degrade
  behaviour on this hardware, not a bug. A smaller Heavy (e.g. a 12–14B) would fit far
  more layers; the budget already handles whatever is registered.
- **Startup Fast trim.** When the GPU is already busy (shared machine), Fast itself
  may load partially offloaded (e.g. `ngl=36`). Functionally fine; just slower until
  VRAM frees up. The budget is conservative by design (768 MiB safety margin).
- **`estimateModelMiB` is file-size based** (≈ on-disk Q4 size + KV + 8 %). It is a
  planning estimate, deliberately generous; the *actual* footprint is read back from
  the backend after load for the VRAM ledger.
- The full `[drain]`/`[lifecycle]` Heavy steps are opt-in (`POLYMATH_E2E_FULL`) to keep
  the default suite green without holding the shared GPU for minutes. They were run for
  real at least once (evidence above) and the model is left unloaded after the test.

## Contract requests
None. No EventBus signal or schema column was needed; the `tasks` table and
`tokenStreamed`/`taskUpdated` signals already covered everything.
