# Wave 1 · Card B — Agent tools, end-to-end

**Read [`../SHARED.md`](../SHARED.md) first.**  Owns: **`src/agent/` only** (runtime,
tool_registry, tools/*).

## Goal
Prove all **16 tools** work and the grammar-constrained tool-calling loop dispatches them.

## Verify
1. **Each tool's `invoke()` directly** (deterministic, no LLM) — for all 16, call
   `invoke(args, ctx)` with valid args and assert the effect: shopping_add/list/remove →
   `shopping_items`; set_reminder/schedule_task → `reminders`/`tasks`; remember/recall/
   search_memory → memory; draft_document/generate_lab_report → a file under `documents/`;
   print_document/print_image → QPrinter path (headless mock); web_search/fetch_page → results
   (stub the HTTP layer or use a local fixture — no live internet); camera_snapshot/who_is_home
   → VisionService stub; queue_deep_task → a queued task row.
2. **One LLM-in-the-loop round-trip** (Fast model) — send *"add milk to my shopping list"* →
   assert the model emits a valid `shopping_add` tool call (GBNF grammar), it executes, the
   result is fed back, and the final reply confirms. Proves registry + grammar + dispatch.

## How
- Read `src/agent/runtime.*`, `tool_registry.*`, `tools/*`, `i_tool.h`.
- `tests/test_agent_e2e.cpp`: a temp/fixture DB + `ToolContext`; loop the registry asserting each
  `invoke`; then one `AgentRuntime` turn with the Fast model (mark slow / skip if no model
  present so the suite still runs).

## Done when
`ctest -R agent` passes: all 16 `invoke()` effects asserted + one LLM-driven tool round-trip.
Report at `docs/sessions/reports/B-agent-tools.md`.
