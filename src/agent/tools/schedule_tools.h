#pragma once
//
// schedule_task / list_schedules / cancel_schedule — Scheduler v2 (overhaul2
// D1): timed/recurring agent goals. Tools only persist rows into
// `scheduled_goals`; the ProactiveEngine (scheduler module, existing 30s
// tick()) polls that table and fires real goals through the A2 execution
// path, so these tools never fire directly (same separation as set_reminder /
// ProactiveEngine's `reminders` table). Implementation in schedule_tools.cpp.
//
// These absorb queue_deep_task's "do something later" niche for agent-goal
// work (a skill or a free-form prompt, run through the full plan/execute/
// reflect harness) — queue_deep_task itself is left in place for heavy
// ITool-only background jobs (research/lab_report/summary) drained by
// TaskScheduler when idle, a different mechanism.
//
#include "i_tool.h"

namespace polymath {

// {title, when:{at?|every_s?|rrule?, anchor?}, task:{skill?|prompt?, params?}, deliver?}
// Exactly one of when.at/every_s/rrule; exactly one of task.skill/task.prompt.
// `anchor` ("HH:MM") pins the local clock time of the first every_s/rrule
// occurrence (today, else tomorrow); defaults to "now" for every_s and "one
// interval from now" for rrule. deliver defaults to "chat".
class ScheduleTaskTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

// {include_disabled?} — list scheduled_goals rows (enabled by default).
class ListSchedulesTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

// {id} — soft-cancel: sets enabled=0 (row kept for history; distinct from the
// Tasks UI's hard-delete "trash" action on ScheduledGoalsModel).
class CancelScheduleTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
