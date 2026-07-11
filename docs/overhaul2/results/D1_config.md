# D1 — config notes

No new `src/core/config.h` key was needed: quiet-hours gating reuses the existing
`keys::QuietHoursStart`/`QuietHoursEnd` (same as ProactiveEngine's reminders path).

One design note for **A4** (SafetyPolicy, not yet landed — `docs/overhaul2/PROGRESS.md`
still shows it unchecked) rather than a config-key request:

`schedule_task` is registered `ToolRiskClass::WriteLocal` (`src/agent/tools/register_tools.cpp`).
The DAG card asks for "Confirm class if rrule/every — standing rules need a yes", but
`ToolRiskClass` is a single static class per tool name (`ToolRegistry::add`), not something
that can vary per invocation from the registration site. A4's `SafetyPolicy::check(toolName,
riskClass, argsJson)` already takes `argsJson`, so the natural place to escalate is there:
when `toolName=="schedule_task"` and `argsJson.when` contains `every_s` or `rrule` (i.e. not
a one-shot `at`), treat it as a standing rule and require Confirm even though the tool's
static registration is WriteLocal. No schema/plumbing change needed on D1's side — just
flagging the intent so A4 doesn't miss it.
