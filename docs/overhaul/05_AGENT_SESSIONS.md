# 05 — Agent Session Manager ("transparent cowork")

Polymath spawns, monitors, and relays **external AI CLI agent sessions** — Claude Code
first, with an adapter layer so Codex, Grok CLI, and arbitrary CLIs plug in. This is the
delegation half of the architecture decision: local model = companion/router; external
agents = deep coding/research/computer-use executors. UX goal: live session cards in the
GUI, voice/toast notification the moment a session needs input, reply from Polymath chat or
voice ("yo, tell Claude yes").

## 1. Architecture

New service `src/sessions/` (own CMake lib `pm_sessions`, linked into pm_app; service on
its own QThread like AudioService):

```cpp
// src/sessions/i_agent_provider.h
struct AgentEvent {                      // normalized cross-provider event
    QString session_id;
    enum Kind { Started, Thinking, ToolUse, AssistantText, NeedsInput,
                PermissionRequest, Result, Error, CostUpdate } kind;
    QString text;                        // human-readable payload (last message, question…)
    QString raw_json;                    // provider-native event for the drill-down view
    double  cost_usd = 0; qint64 ts = 0;
};
class IAgentProvider {                   // one per CLI product
public:
    virtual QString name() const = 0;                      // "claude-code" | "codex" | "pty"
    virtual bool available() const = 0;                    // binary on PATH?
    virtual QString spawn(const SpawnSpec&) = 0;           // returns session_id
    virtual void    send(const QString& id, const QString& text) = 0;
    virtual void    stop(const QString& id) = 0;
    // emits: void event(const AgentEvent&);  (Qt signal on the service thread)
};
struct SpawnSpec { QString provider, cwd, prompt, title; QStringList extra_args;
                   bool resume = false; QString resume_id; };
```

`AgentSessionService` (src/sessions/agent_session_service.h/.cpp): registry of providers;
session table persisted to SQLite (`agent_sessions`: id, provider, title, cwd, status,
native_session_id, cost_usd, created/updated, last_message); republishes normalized events
onto EventBus (`agentSessionEvent` payload — added alongside SurfaceRequest in node A2's
event_bus edit? NO — see 06_DAG: A2 adds BOTH payload structs in its single event_bus
edit so event_bus.h is touched exactly once).

## 2. Providers

### 2.1 ClaudeCodeProvider (src/sessions/providers/claude_code_provider.h/.cpp)
- Spawn: `claude -p "<prompt>" --output-format stream-json --verbose --include-partial-messages=false`
  via QProcess, cwd = spec.cwd. Resume: `claude --resume <native_id> -p "<text>" --output-format stream-json`.
- Parse stdout line-JSON events: `system/init` (capture native session_id, model),
  `assistant` (message text → AssistantText), `result` (Result + cost/usage fields),
  permission-denied / plan-approval / AskUserQuestion shapes → **NeedsInput** with the
  question text. Non-zero exit or `is_error` → Error.
- `send()` = spawn a `--resume` continuation with the user's text (headless `-p` sessions
  are turn-based; each send is a resume). Status while a process runs = working;
  process exits with a result that asks something → needs_input; else done.
- Detect availability: `claude --version` succeeds.
- **Permission policy: never auto-approve.** Anything that looks like a permission or
  plan-approval request is relayed to the user (NeedsInput); the reply goes back via
  `send()`. Polymath must not bypass the CLI's own safety gates.

### 2.2 CodexProvider
Same shape over `codex exec --json "<prompt>"` (and `codex exec resume`); map its event
stream to AgentEvent. Availability-gated; implement to the same interface but mark
`experimental: true` in the model (UI shows a badge) — exact flags verified at
implementation time against the installed CLI version.

### 2.3 GenericPtyProvider (catch-all: grok, aider, anything)
Wrap any CLI in a ConPTY (QProcess with merged channels as v1; full ConPTY escalation only
if needed). Config-driven state detection: per-profile regexes for needs_input / done /
error + idle-timeout heuristic (no output for N s while process alive → possibly waiting →
NeedsInput "(session appears to be waiting)"). Profiles in `data/agent_providers/*.json`
({name, command, args_template, needs_input_patterns[], done_patterns[]}). Honest but
lossy — cards show raw tail output.

## 3. UI surface

- `SessionsModel` (`src/ui/models/sessions_model.h/.cpp`, QAbstractListModel, ctx property
  `agentSessions`): roles id, provider, title, cwd, status
  (working|needs_input|done|error|stopped), lastMessage, costUsd, elapsed, unreadPing.
- **AgentSessionsView.qml** (new page "Agents", section hue #7FE0FF, icon `terminal`):
  grid of live session GlassCards — provider badge, title, status PmStatusDot
  (pulsing while working, amber when needs_input), last message (2-line elide), cost,
  elapsed; card actions: Reply (inline PmTextField), Open drill-down (event log ListView
  from raw_json), Stop. Header actions: "New session…" PmDialog (provider combo, cwd
  picker limited to `agents.allowed_dirs`, prompt).
- **Notifications**: NeedsInput → EventBus Notice (level warn, source "Agents") → toast +
  notification center + optional TTS ("Claude needs input on <title>") when
  `agents.speak_needs_input` (default on). MonitorSurface type (02 §Feature 5) can pin a
  compact session card onto the SurfaceHost ("watch my agents while YouTube plays").

## 4. Tools (local model — registered in register_tools.cpp, risk class `external`)

- `agent_spawn {provider, cwd, prompt, title}` → session id. cwd MUST be inside
  `agents.allowed_dirs` (config, Settings-editable) or the tool returns a refusal telling
  the model to ask the user.
- `agent_send {id, text}`, `agent_status {id?}` (one/all summaries), `agent_stop {id}`,
- `agent_watch {notify: voice|toast}` — subscribes the current goal to session events
  (used by slop_mode skill: "tell me if Claude needs anything").
- Concurrency cap `agents.max_concurrent` (default 2).

## 5. Settings + security

- `agents.allowed_dirs` (semicolon list; empty = spawning disabled, monitor-only),
  `agents.max_concurrent`, `agents.speak_needs_input` — all in Settings ▸ Agents (02).
- Sessions inherit the user's own CLI auth (no keys handled by Polymath).
- Every spawn/send is ActivityLog-recorded; session cards make the whole thing transparent
  (that's the point: a *transparent* cowork).

## 6. Tests

Deterministic: provider registry + availability gating; SpawnSpec validation
(allowed_dirs enforcement); stream-json fixture parsing → AgentEvent sequences (record 3
real fixture transcripts: simple result, needs-input question, error); SessionsModel state
transitions; EventBus → NotificationsModel delivery. Live (skip-green if `claude` not on
PATH): spawn `claude -p "say READY"` in a temp dir → Result event with text containing
READY; cost captured.
