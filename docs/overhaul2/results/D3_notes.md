# D3 — Advisor / supervisor persona + skills

## Files created / touched (D3-owned only)

| Path | Change |
|------|--------|
| `assets/personalities/advisor/persona.json` | **NEW** — Advisor persona bundle |
| `data/skills/daily_briefing/skill.json` | **NEW** — multi-source briefing skill |
| `data/skills/standup_checkin/skill.json` | **NEW** — standup check-in skill |
| `data/skills/project_review/skill.json` | **NEW** — external review + digest |
| `data/skills/session_digest/skill.json` | **NEW** — agent session digest |
| `src/scheduler/proactive_engine.h` | Session-completion slot + debounce |
| `src/scheduler/proactive_engine.cpp` | `agentSessionEvent` → optional `session_digest` fire |
| `docs/overhaul2/results/D3_notes.md` | this file |

**Not edited (by design):** `agent_loop.*` (D2), `register_tools.*`, system/screen tools,
`Main.qml`, `SettingsView`, `capture_views`, `personality_manager.*` (seed-on-activate
would live there — manual schedule seed instead).

No avatar image shipped (optional). Drop `avatar.png` beside `persona.json` later if desired.

## Advisor persona

- **Folder / display name:** `assets/personalities/advisor/` → name `"Advisor"`, wake `"Advisor"`.
- **Voice:** `af_bella` (Kokoro id; distinct from default `af_heart` and the starter Piper ids).
- **Sampling:** temperature `0.55`, top_p `0.9` (a bit tighter than Ada/Marcus).
- **preferred_model:** `fast`.
- **System prompt themes:** advise/supervise/manage; clarifying questions; `remember` for
  commitments; review-not-do; `screen_describe` for "how's this look?"; monitor agents;
  prefer `run_skill` for known workflows; explicit boundary against fs/shell work.

### Tool allowlist (`tools` non-empty ⇒ gated; empty would mean all)

```
recall
remember
web_search
fetch_page
youtube_search
screen_capture
screen_describe
agent_status
agent_spawn
agent_send
agent_watch
schedule_task
list_schedules
cancel_schedule
search_memory
ui_control
run_skill
```

**Explicitly not allowed (default deny via allow-list):** `fs_write`, `fs_delete`,
`run_command`, and other write/destructive system tools. Accept criterion: with Advisor
active, free-form model turns never see `fs_write` in tool specs.

**Note on skill steps:** skill JSON can still invoke tools like `shopping_list` inside a
`kind: "tool"` step — allow-list filters *model-offered* tools (`ToolRegistry::specs`), not
deterministic skill plan steps. That keeps `daily_briefing` complete without putting
shopping mutation tools on the Advisor free-form surface.

Starter bundles are copied from `assets/personalities/` into `data/personalities/` only
when the data dir is empty (`seedStarterBundles`). If you already have personas in
`data/personalities/`, copy the `advisor/` folder in manually (or delete data personas and
re-seed on next start).

## Skills

| Skill | Purpose | Key steps |
|-------|---------|-----------|
| `daily_briefing` | Weather-free multi-source brief | `search_memory`, `recall`, `list_schedules`, `shopping_list`, `agent_status`, synthesize prompt |
| `standup_checkin` | Supervisor standup | `recall`, `list_schedules`, `agent_status`, prompt Qs |
| `project_review` | Optional `{path}`, `{focus}` | `agent_session` spawn review → `agent_status` → digest prompt |
| `session_digest` | Optional emphasize `{session_id}` | `agent_status` (all) → spoken digest prompt |

Triggers include phrases like "give me my briefing", "standup", "project review",
"session digest" / "how are my agents".

`data/skills/*` is typically gitignored under `/data/` — files are still created here;
orchestrator force-adds if needed.

## Seed schedules (manual — no personality_manager edits)

Auto seed on first Advisor activation would require `personality_manager` write hooks
(E2 territory). Prefer manual setup once Advisor is active (tools available via allow-list):

### 1) Morning briefing 08:00 daily (voice)

Chat/tool shape for `schedule_task`:

```json
{
  "title": "Morning briefing",
  "when": { "rrule": "FREQ=DAILY", "anchor": "08:00" },
  "task": { "skill": "daily_briefing", "params": {} },
  "deliver": "voice"
}
```

Or standup instead:

```json
{
  "title": "Standup check-in",
  "when": { "rrule": "FREQ=DAILY", "anchor": "09:00" },
  "task": { "skill": "standup_checkin", "params": {} },
  "deliver": "voice"
}
```

### 2) Session-completion digest (event-triggered)

Insert a row the ProactiveEngine hook recognizes. Easiest via SQL / DB browser, or a
one-shot that leaves `next_fire` null and never time-fires:

```sql
INSERT INTO scheduled_goals(
  title, prompt, skill, params_json, kind, spec, next_fire,
  last_fire, enabled, deliver, source, created_at
) VALUES (
  'Session digest on complete',
  '',
  'session_digest',
  '{}',
  'at',
  '',
  NULL,
  NULL,
  1,
  'voice',
  'event:agent_session',
  strftime('%s','now')
);
```

**Contract:** `enabled=1`, `skill='session_digest'`, `source='event:agent_session'`.
`kind`/`spec`/`next_fire` are ignored for the event path (only `last_fire` is updated).
Disable with `cancel_schedule` (enabled=0) or Tasks ▸ Scheduled trash.

## ProactiveEngine hook (minimal)

On `EventBus::agentSessionEvent` with `kind` ∈ {`Result`, `Error`}:

1. Look up the seed row above (LIMIT 1).
2. Debounce 45s (`last_session_digest_unix_`).
3. Quiet-hours gate same as timed schedules (`notify` bypasses).
4. Create `origin=schedule` goal + single `skill` plan_step `session_digest` with
   `params.session_id` set from the event; `requestGoalExecution`.
5. Do **not** advance `next_fire`.

No other proactive_engine behaviour changed (D1 tick / voice echo intact).

## Accept checklist

| Criterion | How |
|-----------|-----|
| Activate Advisor + "give me my briefing" | Triggers `daily_briefing` skill / multi-source digests |
| Standup schedule fires and speaks | Seed standup/daily schedule with `deliver=voice` |
| Tool gating | Advisor `tools` omits `fs_write` / `run_command` / `fs_delete` |
| Session digest on complete | Seed `source=event:agent_session` row; complete an agent session |

## Deviations / follow-ups

- No first-activation auto-seed (would need personality_manager).
- No `avatar.png`.
- `project_review` without a valid `path` under `agents.allowed_dirs` will fail spawn and
  fall through to the digest prompt's "couldn't spawn" branch (by design).
- `shopping_list` is skill-only for Advisor free-form turns; add to allow-list later if
  conversational shopping management is desired without switching persona.
