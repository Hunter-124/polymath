#pragma once
//
// Lab-session tools — the state machine driving the interactive guided lab agent
// over the v2 `lab_sessions` / `lab_session_steps` tables. State lives entirely
// in the DB (no in-memory session map) so a session survives a restart.
//
// Lifecycle: start_lab_session -> [next_lab_step / verify_lab_step]* ->
// finish_lab_session (which hands verified data to generate_lab_report).
// Each transition emits a LabStepEvent on the bus for live UI/mobile display.
// Implementations in lab_session.cpp.
//
#include "i_tool.h"

namespace polymath {

class StartLabSessionTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class NextLabStepTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class VerifyLabStepTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class FinishLabSessionTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
