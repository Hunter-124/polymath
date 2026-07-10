#pragma once
//
// Agent session tools (overhaul 05 §4) — local-model interface to
// AgentSessionService via QObject Q_INVOKABLE methods (no hard link to
// pm_sessions). Risk class: external. Registration owned by C5.
//
//   agent_spawn  {provider, cwd, prompt, title} → session id
//   agent_send   {id, text}
//   agent_status {id?}  — one or all summaries
//   agent_stop   {id}
//   agent_watch  {notify: voice|toast} — subscribe current goal to session events
//
#include "i_tool.h"

class QObject;

namespace polymath {

// Shared resolve of the live service (set via setAgentSessionService / deps).
QObject* resolveAgentSessionService();

class AgentSpawnTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class AgentSendTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class AgentStatusTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class AgentStopTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class AgentWatchTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
