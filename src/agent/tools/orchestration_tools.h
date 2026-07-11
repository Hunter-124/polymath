#pragma once
//
// spawn_subtask / subtask_status — Goal-tree orchestration (overhaul2 D2).
//
// spawn_subtask creates a child goals row (parent_id set) with one or more
// plan_steps (prompt / skill / agent_session) and hands it to
// requestGoalExecution. Pure-local children run sequentially via the single
// agent worker; agent_session children run in parallel through
// AgentSessionService (bounded by agents.max_concurrent).
//
// The parent parks waiting_children when its own steps finish while children
// are still live; join_policy (all|any|first_success) decides when to resume
// with a results digest (AgentLoop).
//
// Depth cap 2 / child cap 8 are enforced here (AgentLoop::kGoalTree*).
//
#include "i_tool.h"

namespace polymath {

// {title, prompt?|skill?|task:{skill?|prompt?}, kind?, parent_id?, join_policy?,
//  provider?, args?}
// Exactly one of prompt / skill (top-level or under task). parent_id defaults
// to the goal currently inside executeGoal. join_policy is stored on the
// *parent* (all|any|first_success).
class SpawnSubtaskTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

// {parent_id?} — list children of a parent goal with status + summary.
// parent_id defaults to the executing goal.
class SubtaskStatusTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
