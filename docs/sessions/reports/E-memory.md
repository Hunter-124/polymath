# Card E — Memory & recall, end-to-end — Report

**Status: PASS.** `ctest -R memory` green; full suite `ctest` 3/3 green (core, tools, memory).
The four card requirements are proven end-to-end against the real `src/memory/` stack with
real EmbeddingGemma embeddings and a real LLM run for the summarizer.

## How verified
- New integration test `tests/test_memory_e2e.cpp` (registered as ctest `memory`), run on the
  CPU build with the junctioned `data/models` tree.
- DB + vector index live in a throwaway temp dir; models resolve from `<exe>/data/models`
  exactly like `main.cpp resolveAppRoot()`. Models are registered into the `models` table by
  absolute path so the InferenceManager loads them without scanning the temp root.

### 1. Vector recall — VERIFIED
- 5 themed memories inserted via `MemoryService::remember()` → embedded (EmbeddingGemma,
  **dim 768**) → indexed in hnswlib.
- Related query *"fixing a dripping water tap in the kitchen"* → top hit is the plumbing
  memory (#1) at cosine **0.747**.
- Unrelated query *"booking flights to Japan for a vacation"* → top hit is #2 at **0.333**
  (a different item, clearly weaker). Asserts: related query surfaces the right item in top-3,
  unrelated query does not return it as #1 and scores strictly lower.
- Backfill path also exercised (a row written before the index opened got indexed on open).

### 2. Persistence round-trip — VERIFIED
- After `MemoryService::stop()` persists the graph, the store is fully reopened: **5 rows,
  5 `vector_id`s survive**, and recall still returns the plumbing memory against the
  *reloaded* HNSW file (proves the on-disk graph round-tripped, not just the DB rows).

### 3. Daily summarizer — VERIFIED (real LLM)
- A day of fixtures (5 ambient/command transcripts + 2 events) fed to `Summarizer::summarizeDay()`
  directly (not via the scheduler).
- Real generation with the resident Fast model (gemma-3n-E4B) produced a **535-char digest**
  plus **8 actionable follow-ups** (4 suggestions stored as `source='suggestion:...'` memories +
  4 rows in the `reminders` table). A `kind='summary'` memory row is persisted for the day.
- The summarizer step is **opt-in-graceful**: if no fast/heavy LLM is registered it asserts
  structure only (fixtures load, `summarizeDay()` degrades to an empty digest without crashing)
  so the default suite stays green on a model-less box. `POLYMATH_TEST_SUMMARIZER=1` forces a
  real digest+follow-up.

### 4. Retention sweeper — VERIFIED
- Seeded: expired-TTL transcript, future-TTL transcript, 60-day-old ambient transcript,
  1-hour-old ambient transcript, 200-day-old event, 1-hour-old event.
- `MemoryService::runRetentionSweep()` called directly → expired-TTL + ancient-ambient
  transcripts and the old event purged; future-TTL + fresh ambient transcripts and the fresh
  event survive (2 transcripts + 1 event kept). Robust against the seeded windows
  (`retention.ambient_days=7`, `retention.events_days=30`).

## Files changed
- `tests/test_memory_e2e.cpp` — **new**. The end-to-end test above.
- `tests/CMakeLists.txt` — **append-only**. Added `test_memory_e2e` target + ctest `memory`
  (links `pm_core pm_inference pm_memory Qt6::Core`).

No `src/memory/` source changes were needed — the store, vector index, summarizer, and
retention sweeper all behaved correctly as written. No frozen contracts touched.

## Broken / fixed
- Nothing broken in `src/memory/`. The only fix was within the new test: the initial draft
  asserted `embeddingModelOnDisk()` against the temp `Paths` root (empty); corrected to check
  the real `data/models` dir. (Two iterations: first run asserted on the wrong dir; fixed and
  re-ran green.)

## Residual gaps
- **Summarizer requires a resident LLM for the full assertion.** On a box with no fast/heavy
  model the summarizer sub-step is structure-only (degrades cleanly, no digest). This is by
  design per the card. With a model present (this run) it asserts a real digest + ≥1 follow-up.
- **No grammar-constrained decode in this path.** The summarizer uses a plain (un-grammared)
  ChatRequest, so the inference-engine grammar crash flagged in the card brief was not on this
  path and was not hit. No `src/inference` edits were needed or made.
- The summarizer's prose/JSON parsing depends on the model emitting the trailing fenced JSON
  block; the model used here complied. A weaker model that omits the block would yield a digest
  with zero follow-ups — the test would then (correctly) treat it as a non-actionable run.

## Contract requests
- **None.** No EventBus signal or schema column changes were required.
