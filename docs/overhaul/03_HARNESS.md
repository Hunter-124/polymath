# 03 — Agent Harness v2: goals, plans, skills, memory

Rework of `src/agent` + `src/tools`-adjacent seams from a flat single-turn tool-caller into
a persistent, resumable, background-capable agent harness. External-agent delegation is
spec'd separately in 05 (it plugs into this as an executor).

## 0. Audit summary (what v1 actually does — verified)

- `AgentRuntime::runTurn` (agent_runtime.cpp:187): persona → messages = [system + tool
  catalog] + last **8 transcript rows (all mislabeled Role::User)** + user turn → GBNF-
  constrained loop capped at `kMaxToolRounds=6` → unconstrained final answer streamed.
  One turn at a time (`busy_` CAS). No token counting/compaction.
- **Deep-task disconnect**: `TaskScheduler::runTask` (task_scheduler.cpp:183) runs a bare
  completion chosen by `type` string — it never dispatches `ITool::invoke` and never calls
  `MemoryService::summarizeDay`. `taskFinished` is connected to nothing → results die in
  `tasks.result_json`.
- **Memory disconnect**: `ToolContext` (i_tool.h:17) = {InferenceManager*, Database*,
  user, personality} — no MemoryService. `recall`/`search_memory` are keyword-LIKE.
  `MemoryService::recall()` (real hnswlib semantic path) has no callers in the agent loop.
  Nothing injects memories into prompts.
- Grammar/tooling layer (grammar.cpp GBNF + grammar-checked resampling) is solid — keep it.

## 1. Persistence schema (new tables; add to `core/schema.h` migrations)

```sql
CREATE TABLE goals (
  id INTEGER PRIMARY KEY, title TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'active',
    -- active | waiting_user | waiting_agent | done | failed | cancelled
  origin TEXT NOT NULL DEFAULT 'chat',      -- chat | voice | schedule | skill | agent
  context_json TEXT NOT NULL DEFAULT '{}',  -- params, originating request_id, skill name…
  result_json TEXT, created_at INTEGER, updated_at INTEGER);
CREATE TABLE plan_steps (
  id INTEGER PRIMARY KEY, goal_id INTEGER NOT NULL REFERENCES goals(id),
  idx INTEGER NOT NULL, description TEXT NOT NULL,
  kind TEXT NOT NULL,                       -- tool | prompt | skill | agent_session | surface
  tool TEXT, args_json TEXT, status TEXT NOT NULL DEFAULT 'pending',
    -- pending | running | done | failed | skipped
  result_json TEXT, attempts INTEGER NOT NULL DEFAULT 0, updated_at INTEGER);
```
Startup recovery: `running` steps → `pending`; `active` goals resume when their executor
becomes available (idle scheduler or explicit user ping).

## 2. AgentLoop v2 (`src/agent/agent_loop.h/.cpp` — new; AgentRuntime becomes a thin owner)

One reusable loop object invoked from BOTH the interactive turn path and the background
scheduler. Runs on the calling worker thread; all UI contact stays via EventBus.

### 2.1 Turn router (interactive path)
Fast-model classification with a 3-way grammar: `quick` (answer directly / ≤2 leaf tool
calls — v1 behavior, keep it snappy), `goal` (multi-step: create a goal + plan), `command`
(pure UI/skill invocation, e.g. "put YouTube on" → run_skill/ui_control, no plan needed).
Router prompt gets the skill catalog + tool names only (cheap).

### 2.2 Goal execution: plan → execute → reflect
- **Plan**: constrained JSON generation → `{title, steps:[{description, kind, tool?, args?}]}`,
  max 12 steps, persisted before execution. Skills expand to pre-authored steps (§4) —
  no LLM planning needed for them.
- **Execute**: steps sequentially (parallelism NOT in scope — single inference thread
  anyway). Each step: mark running → dispatch by kind:
  - `tool` → `ITool::invoke` (existing dispatch, incl. isDeepTask re-queue),
  - `prompt` → completion with step description + accumulated context,
  - `skill` → expand inline,
  - `agent_session` → AgentSessionService spawn/await (05) — step parks the goal in
    `waiting_agent`; session completion events resume it,
  - `surface` → publish SurfaceRequest.
  Persist result_json + status after every step (crash-resumable).
- **Reflect**: on step failure (attempts < 3): one reflection completion — "step X failed
  with E; revise remaining plan" → constrained re-plan of the *remaining* steps only.
  Attempts ≥ 3 or reflection says unrecoverable → goal `failed` with a summary.
- **Loop guards**: wall-clock cap per goal (config `agent.goal_timeout_min`, default 30),
  total-step cap 24, per-step token caps as today.

### 2.3 Delivery (fixes the black hole)
On goal terminal state, publish `GoalUpdate` on EventBus (new payload: goal id, title,
status, summary). AppController relays → NotificationsModel (02) + chat injection (an
assistant message "✔ Finished: <title> — <summary>") + optional TTS if
`agent.speak_results` (default on for voice-originated goals only). Wire the existing
orphaned `TaskScheduler::taskFinished` into the same path.

### 2.4 Context assembly v2 (used by every generation)
Token-budgeted via `llama_tokenize` on the Fast model (exposed through InferenceManager;
add `int countTokens(QString)` — cheap, no generation). Budget for n_ctx 4096:
system+persona+tool catalog ≤ 1100 · semantic memories ≤ 400 (MemoryService::recall top-5
on the user turn, injected as a "Relevant memories:" block) · rolling summary ≤ 400
(maintained per-conversation: when history > budget, summarize oldest half via Fast model,
store in `conversation_summaries`) · verbatim recent turns ≤ 1400 (correct roles — fix the
Role::User mislabeling) · reserve ≥ 700 for generation + tool results. Tool results > 1200
tokens get one-line-per-item compaction before entering context.

## 3. Fix the disconnects (surgical, lands before the loop rewrite — node A3)

1. `ToolContext` gains `MemoryService* memory` (i_tool.h) — threaded through
   agent_runtime + task_scheduler; `recall`/`search_memory`/`remember` use semantic
   embed→search with keyword fallback when the embedder is unavailable.
2. `TaskScheduler::runTask` dispatches by kind: known tool name → construct ToolContext →
   `ITool::invoke`; `daily_summary` → `MemoryService::summarizeDay()`; else completion
   (legacy). Queued `generate_lab_report` must produce a real .docx (test-gated).
3. `taskFinished` → AppController connection → notification + chat delivery (§2.3 path,
   even before goals exist).
4. History roles fixed (transcripts rows carry who; map to Role::User/Assistant).

## 4. Skills — declarative, AI-authorable workflow modules

Location: `data/skills/<name>/skill.json` (user-editable, hot-reloaded via file watcher).
```jsonc
{ "name": "slop_mode",
  "description": "Ambient YouTube + agent-session babysitting",
  "triggers": ["slop mode", "put something on and watch my agents"],
  "params": { "type":"object", "properties": { "topic": {"type":"string"} } },
  "confirm": false,                       // true → ask user before running
  "steps": [
    { "kind":"surface", "args": { "action":"spawn", "type":"web", "title":"YouTube",
        "args": { "url":"https://youtube.com/results?q={topic}", "mode":"video" } } },
    { "kind":"tool", "tool":"agent_watch", "args": { "notify":"voice" } } ] }
```
- `SkillRegistry` (`src/agent/skills/skill_registry.h/.cpp`): load/validate/watch; exposes
  the catalog to the router prompt and to a `run_skill` tool
  (`{name, params}` → expand steps with `{param}` substitution → goal).
- `save_skill` tool: the model can author a new skill from a described routine
  (`confirm:true` forced on AI-authored skills until the user edits them).
- Ship 3 starter skills: `slop_mode` (above), `morning_brief` (weather-less v1: reminders
  due today + yesterday summary + task queue status → spoken), `research_brief`
  (web_search×3 + fetch_page + summarize → notification + document).

## 5. Permissions

Tool risk classes in registration metadata: `read` (auto), `write_local` (auto, logged),
`external` (network side effects — auto but logged + notification), `spend`/`destructive`
(require confirmation — v1: goal parks `waiting_user`, notification + chat prompt, user
confirms in chat/palette). Persona `tools` allow-list unchanged. Agent-session spawning is
`external` + directory-allowlisted (05).

## 6. Observability

Every goal keeps a JSON trace (steps, args, truncated results, timings, token counts) in
`goals.context_json.trace`. TaskQueueView (GUI) gains a goal drill-down: step list with
status dots (amber section styling per 01). ActivityLog keeps recording per-tool lines.

## 7. Tests (extend `tests/test_agent_e2e.cpp` + new `tests/test_harness_e2e.cpp`)

Deterministic (no model needed): schema migration; goal persistence + crash-resume
(kill mid-step → restart → resumes); TaskScheduler dispatches a real tool (queued
generate_lab_report → .docx exists); taskFinished delivery reaches NotificationsModel;
skill loading/validation/param substitution; ToolContext.memory wired (semantic recall
returns seeded memory with stub embedder). Model-gated (skip-green without GGUF): router
classification on 3 canned utterances; 2-step goal end-to-end; reflection re-plan on a
forced tool failure.
