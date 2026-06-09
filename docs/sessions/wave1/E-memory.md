# Wave 1 · Card E — Memory & recall, end-to-end

**Read [`../SHARED.md`](../SHARED.md) first.**  Owns: **`src/memory/` only** (store, vector_index,
summarizer). Proactive *reminders firing* belong to the scheduler (Card D) — out of scope here;
you verify the memory + summarizer side.

## Goal
Prove persistence, semantic recall, the daily summarizer, and retention.

## Verify
1. **Vector recall** — insert N memories/transcripts → embed (EmbeddingGemma) → query a
   semantically related phrase → the right item is in top-k (and an unrelated query is not).
   Tests the hnswlib index + embeddings.
2. **Persistence round-trip** — write memories → reopen the store → they and their vectors
   survive.
3. **Daily summarizer** — feed a day of fixture transcripts/events → run the summarizer directly
   (not via the scheduler) → non-empty summary + ≥1 actionable suggestion/reminder candidate.
4. **Retention sweeper** — insert rows with an expired TTL → run the sweep → expired purged,
   fresh rows remain.

## How
- Read `src/memory/*` (store, vector_index, summarizer) and the `memories` / `transcripts` tables
  in `src/core/schema.h` (read-only).
- `tests/test_memory_e2e.cpp` with a temp DB + seeded fixtures. The summarizer needs an LLM — use
  the Fast model, or assert structure and mark slow if no model is present.

## Done when
`ctest -R memory` passes: top-k recall correct, persistence survives reopen, summarizer yields
summary+suggestion, retention purges expired. Report at `docs/sessions/reports/E-memory.md`.
