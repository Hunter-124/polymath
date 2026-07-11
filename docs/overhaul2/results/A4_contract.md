# A4 — Risk-gate enforcement (SafetyPolicy + waiting_user): contract

This is the surface node **C1** (Confirmation UX + Safety settings) and **C2/C3**
(system/screen tools) consume. Everything here is landed on `master` by A4.

## 1. SafetyPolicy (pm_core, `src/core/safety_policy.{h,cpp}`)

`class polymath::core::SafetyPolicy` — thread-safe (a mutex guards a compiled
cache of roots/regexes), Config-backed, **no dependency on pm_agent**.

```cpp
enum class core::RiskLevel { Read, WriteLocal, External, Spend, Destructive }; // auto-allow rank
enum class core::Decision  { Allow, Confirm, Deny };
struct core::Ruling { Decision decision; std::string reason; };

core::Ruling SafetyPolicy::check(const std::string& tool,
                                 core::RiskLevel risk,
                                 const nlohmann::json& args) const;
bool SafetyPolicy::auditEnabled() const;
static std::string SafetyPolicy::describe(tool, args);     // human confirm line
static std::string SafetyPolicy::argsPreview(args);        // truncated JSON for the dialog
```

### Decision rules (Deny always wins over Confirm)
1. **Path args** (keys `path/src/source/dst/dest/destination/file/filepath/filename/cwd/dir/directory/target`):
   Deny if the normalized path matches a denied glob, or sits outside every
   allowed root (when roots are configured).
2. **Command args** (keys `command/cmd/commandline/script`, plus an `args` array):
   Deny if any denylist regex matches.
3. **Write size**: when a path arg is present and `content/text/body/data` exceeds
   `safety.max_file_write_kb`, Deny.
4. **Risk/mode gate**: `Destructive` is **never** auto-allowed (always Confirm
   unless already Denied). Otherwise Allow iff `rank(risk) <= effectiveCeiling`,
   else Confirm.

### Mode ↔ ceiling interaction (how `safety.mode` + `autoconfirm_risk_max` combine)
`effectiveCeiling = min(autoconfirm_risk_max + modeShift, Spend)` where
`modeShift = strict:0, standard:+1, trusted:+2`. With the default base
`autoconfirm_risk_max = write_local`:

| mode | auto-allowed | confirmed |
|------|--------------|-----------|
| strict   | Read, WriteLocal | External, Spend, Destructive |
| standard | Read, WriteLocal, External | Spend, Destructive |
| trusted  | Read, WriteLocal, External, Spend | Destructive |

Standard mode reproduces the historical `toolRiskRequiresConfirmation` baseline
(Spend/Destructive confirm) **and** B1's "External auto-allowed by default
policy" — so YouTube/web tools stay frictionless. Destructive is hard-gated even
if someone sets the ceiling to `destructive`.

> Note: the DAG lists the base default as `write_local`; that literal value is
> kept. The *effective* standard-mode ceiling is External via the mode shift, so
> External auto-runs by default (required by B1 + the owner's frictionless-media
> priority + `test_goals`). Documented here rather than silently deviating.

### ToolRiskClass → RiskLevel mapping
`tool_registry.h` gains `inline core::RiskLevel toRiskLevel(ToolRiskClass)`.
The agent calls it at the dispatch site: `safety_.check(tool, toRiskLevel(tools_.riskOf(tool)), args)`.
pm_core never sees `ToolRiskClass`.

### Allowed roots
`safety.fs_allowed_roots` is `;`-separated. Tokens `Documents`/`Desktop`/
`Downloads` resolve via `QStandardPaths`; `@data` resolves to `Paths::root()`;
anything else is a literal path. **`agents.allowed_dirs` are unioned in** so a
directory already authorized for external agent sessions (agent_spawn's `cwd`)
is not denied by the fs gate.

## 2. Config keys (added to `config.h` keys + `seedDefaults`)

| key | default | meaning |
|-----|---------|---------|
| `safety.mode` | `standard` | strict \| standard \| trusted |
| `safety.autoconfirm_risk_max` | `write_local` | base auto-allow ceiling (shifted by mode) |
| `safety.fs_allowed_roots` | `Documents;Desktop;Downloads;@data` | allowed fs roots (∪ agents.allowed_dirs) |
| `safety.fs_denied_globs` | `*/.git/*;C:/Windows/*;C:/Program Files*;C:/Program Files (x86)*;*/polymath.db;*/polymath.db-wal;*/polymath.db-shm` | always-denied globs (deny wins). AppData is NOT globbed — it's protected by the allowed-roots whitelist instead, so the installed `%LOCALAPPDATA%/Polymath` data root stays writable |
| `safety.cmd_denylist` | `format`, `del /s\|/q\|/f`, `rd /s`, `rm -rf`, `Remove-Item -Recurse`, `reg add\|delete`, `shutdown`, `diskpart`, `mkfs`, `fdisk`, `cipher /w`, `format-volume`, `clear-disk` (regex list) | denied command patterns |
| `safety.max_file_write_kb` | `2048` | max write payload |
| `safety.audit` | `1` | record every gated call to ActivityLog |

C1's Settings ▸ Safety edits these keys. `safety.tool_overrides` (per-tool
"always allow") is **reserved for C1** — A4 does not read it yet.

## 3. EventBus payloads + signals (`event_bus.h/.cpp`) — C1 consumes these

```cpp
struct ConfirmRequest  { QString id, tool, summary, args_preview, reason; };
struct ConfirmResponse { QString id; bool approved = false; bool always_allow = false; };

signals:
    void confirmRequested(const polymath::ConfirmRequest&);   // AgentLoop → UI
    void confirmResponse(const polymath::ConfirmResponse&);    // UI → AgentLoop
// helpers: publishConfirmRequest(), publishConfirmResponse()
```

Both are `Q_DECLARE_METATYPE`'d + `qRegisterMetaType`'d (queued cross-thread).

**Semantics for C1:**
- On a Confirm ruling AgentLoop publishes **`confirmRequested`** with a unique
  `id` (also persisted). Render `summary` (human line), `args_preview` (pretty
  JSON), `reason`. The pending call is durable (survives restart).
- Reply by publishing **`confirmResponse{ id, approved }`** with the SAME `id`.
  `approved=true` runs the pending call and resumes the parked goal; `false`
  returns the denial to the model (the parked step fails, the goal fails/reflects
  without re-asking). `always_allow` is reserved (C1 wires `safety.tool_overrides`).
- AgentLoop is already subscribed to `confirmResponse` (queued onto the agent
  worker thread); C1 only needs the AppController relay + dialog/notification UI.
- Voice/chat approval ("yes, do it" / "no") already works via the chat path —
  AgentLoop turns a short affirmative/negative reply into a `confirmResponse`.

## 4. Enforcement flow (agent_loop)

`AgentLoop::dispatchToolChecked` (the A2 single choke point) now gates every call:
- **Allow** → invoke as before.
- **Deny** → `ToolResult{ ok:false, content:{error:"denied by safety policy: …"} }`
  the model sees and can adapt.
- **Confirm** → `ToolResult{ ok:false, content:{confirm_required:true, summary,
  args_preview, reason} }` **without invoking**. The caller parks:
  - **Goal path** (`dispatchToolStep`): persist a `pending_confirmations` row,
    publish `confirmRequested`, set the goal `waiting_user` with resume markers
    (mirrors the A2 `waiting_agent` machinery), leave the step pending.
  - **Quick path** (`runQuick`, no goal): wrap the call in a one-step carrier
    goal, park it `waiting_user`, and end the turn with a
    `⚠ Needs your approval: …` chat line.

Resume: `onConfirmResponse` marks the row approved/denied and calls
`requestGoalExecution(goal_id)` (runs under the runtime busy-guard, never
re-entering a live turn). `executeGoal` re-dispatches the parked step;
`dispatchToolStep` consumes the resolved row via `takeConfirmDecision` and
invokes the approved call **bypassing the re-check** (or fails on deny).

New durable table (created idempotently at startup, like `conversation_summaries`):
`pending_confirmations(id, tool, args_json, goal_id, step_idx, request_id,
summary, status, created_at)`.

## 5. Audit
When `safety.audit` is on, every gated invocation appends one `ActivityLog`
row: `tool` + `safety=<decision>` + reason (and `safety=confirmed (approved|denied
by user)` on resume). Uses the existing `events(kind='tool')` feed + retention.

## 6. Tests
`tests/test_safety_policy.cpp` (target `safety_policy`): path allow/deny matrix
(inside/outside roots, denied globs, secret glob), command denylist (rm -rf /
format / Remove-Item -Recurse), mode escalation, Destructive-never-auto, write
cap, deny-wins-over-confirm.
