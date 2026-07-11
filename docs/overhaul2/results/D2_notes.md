# D2 — Goal-tree orchestration (local subagents)

## Files touched
| Path | Change |
|------|--------|
| `src/core/schema.h` | goals: `parent_id`, `join_policy`; status note `waiting_children`; kSchemaVersion **4** |
| `src/agent/agent_loop.h` / `.cpp` | park/join/resume `waiting_children`; `ensureGoalTreeColumns`; caps; `executingGoalId` |
| `src/agent/tools/orchestration_tools.{h,cpp}` | **NEW** `spawn_subtask`, `subtask_status` |
| `src/agent/tools/register_tools.cpp` | register the 2 tools (WriteLocal / Read) |
| `src/agent/CMakeLists.txt` | `orchestration_tools.cpp` |
| `tests/test_agent_e2e.cpp` | builtin tool count 40 → **42** |

## Schema
- `goals.parent_id` INTEGER (NULL/0 = root)
- `goals.join_policy` TEXT NOT NULL DEFAULT `'all'` — `all` | `any` | `first_success`
- Fresh DBs: columns in `CREATE TABLE goals`
- Existing DBs: `AgentLoop::ensureGoalTreeColumns()` (PRAGMA `table_info` + `ALTER TABLE ADD COLUMN`) on `recoverOnStartup` / `createGoal`; tools also ensure before insert

## Caps (hardcoded defaults)
- `AgentLoop::kGoalTreeDepthMax = 2` (root depth 0 → max child depth 2)
- `AgentLoop::kGoalTreeChildCap = 8` per parent  
Config keys `agent.goal_tree_depth_max` / `agent.goal_tree_child_cap` not added (config ownership tight).

## Tools
### `spawn_subtask`
Args: `title` (required), `prompt` / `skill` / `task:{…}`, `kind` (`prompt|skill|agent_session`), `parent_id?`, `join_policy?` (written on **parent**), `provider?` / `args?`.

- Resolves parent: `parent_id` arg → else `AgentLoop::executingGoalId()` (set for the duration of `executeGoal`)
- Enforces depth + child caps
- INSERT child `goals` row (`origin=agent`, `parent_id`), one `plan_steps` row, `requestGoalExecution(child_id)`

### `subtask_status`
Args: `parent_id?` (default executing goal). Returns children list + done/failed/active counts + parent join_policy/status.

## How the parent parks / resumes

```
parent executeGoal
  └─ tool steps may call spawn_subtask N times (children queued)
  └─ when no pending parent steps left:
        maybeParkOrJoinChildren()
          ├─ children still live → status = waiting_children  (park)
          └─ join already satisfied → stash children_digest, reflect-or-done

child reaches terminal (deliverGoalTerminal)
  └─ tryResumeParentAfterChild(child)
        ├─ parent.status != waiting_children → no-op (parent still spawning)
        ├─ join policy not yet satisfied → stay parked
        └─ satisfied → parent status=active, requestGoalExecution(parent)
              └─ maybeParkOrJoinChildren joins + may reflectAndReplan on partial failure
```

### Join policies
| Policy | Satisfied when | Succeeds when |
|--------|----------------|---------------|
| `all` | every child terminal | all `done`, none failed |
| `any` | ≥1 `done` **or** all terminal | ≥1 `done` |
| `first_success` | ≥1 `done` **or** all terminal | ≥1 `done` |

Partial failure (join satisfied but not succeeded) → `reflectAndReplan` with a synthetic fanout step; unrecoverable → parent `failed` with digest.

## Scheduling model
- Pure-local children: serial via single agent worker (`executeGoalOne` + `busy_` + `resumeActiveGoals` FIFO)
- `agent_session` children: can run “in parallel” as external sessions (bounded by `agents.max_concurrent`) while parent sits `waiting_children`

## Accept sketch (manual / future e2e)
1. Create parent goal with a plan that calls `spawn_subtask` twice (prompt) + once (`agent_session` if sessions wired)
2. Parent ends own steps → `waiting_children`
3. Children finish → parent resumes with `context.children_digest` summary
4. Unit: force three children with mixed done/failed; assert `all` / `any` / `first_success` join outcomes
