#pragma once
//
// ToolRegistry — holds every ITool and builds the per-turn tool list (filtered
// by the active personality's allow-list).  The AgentRuntime asks it for the
// JSON-schema tool descriptions and dispatches calls by name.
//
// Risk classes (03 §5) are registration metadata: read / write_local / external
// auto-run (logged / notified as appropriate); spend / destructive park the
// goal waiting_user for confirmation.
//
#include "i_tool.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

class QObject;

namespace polymath {

class SkillRegistry;

// Permission / side-effect class (overhaul 03 §5).
enum class ToolRiskClass {
    Read,         // auto
    WriteLocal,   // auto, logged
    External,     // network / external side effects — auto, logged + notice
    Spend,        // require confirmation
    Destructive,  // require confirmation
};

inline const char* toolRiskClassName(ToolRiskClass r) {
    switch (r) {
    case ToolRiskClass::Read:        return "read";
    case ToolRiskClass::WriteLocal:  return "write_local";
    case ToolRiskClass::External:    return "external";
    case ToolRiskClass::Spend:       return "spend";
    case ToolRiskClass::Destructive: return "destructive";
    }
    return "read";
}

inline bool toolRiskRequiresConfirmation(ToolRiskClass r) {
    return r == ToolRiskClass::Spend || r == ToolRiskClass::Destructive;
}

// Optional deps for tools that need live services (skills / agent sessions).
// sessions is a QObject* to AgentSessionService (Q_INVOKABLE spawn/send/stop/
// status) so pm_agent does not hard-link pm_sessions (CMake order).
// When skills is null, registerBuiltinTools owns a process-local SkillRegistry.
// When sessions is null, agent_* tools refuse until setAgentSessionService().
struct BuiltinToolDeps {
    SkillRegistry* skills   = nullptr;
    QObject*       sessions = nullptr;   // AgentSessionService*
};

class ToolRegistry {
public:
    void add(std::shared_ptr<ITool> tool,
             ToolRiskClass risk = ToolRiskClass::Read);

    ITool* get(const std::string& name) const {
        auto it = tools_.find(name);
        return it == tools_.end() ? nullptr : it->second.get();
    }

    ToolRiskClass riskOf(const std::string& name) const;
    bool requiresConfirmation(const std::string& name) const {
        return toolRiskRequiresConfirmation(riskOf(name));
    }

    // OpenAI-style tool specs for the names allowed this turn (empty = all).
    // Each entry may include "risk" alongside the function block for harness use.
    nlohmann::json specs(const std::vector<std::string>& allow = {}) const;

    std::vector<std::string> names() const;

private:
    std::map<std::string, std::shared_ptr<ITool>> tools_;
    std::map<std::string, ToolRiskClass>          risks_;
};

// Registers all built-in tools (web, docs, print, shopping, reminders, memory,
// camera, queue_deep_task, skills, agent sessions, ui_control).
// Implemented in agent/tools/register_tools.cpp.
void registerBuiltinTools(ToolRegistry& reg, BuiltinToolDeps deps = {});

// Late-bind AgentSessionService (as QObject*) for agent_* tools.
void setAgentSessionService(QObject* sessions);
QObject* agentSessionService();

// Process-local SkillRegistry used when deps.skills is null (tests / runtime).
SkillRegistry* defaultSkillRegistry();

} // namespace polymath
