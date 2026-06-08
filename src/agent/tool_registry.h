#pragma once
//
// ToolRegistry — holds every ITool and builds the per-turn tool list (filtered
// by the active personality's allow-list).  The AgentRuntime asks it for the
// JSON-schema tool descriptions and dispatches calls by name.
//
#include "i_tool.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace polymath {

class ToolRegistry {
public:
    void add(std::shared_ptr<ITool> tool) { tools_[tool->name()] = std::move(tool); }
    ITool* get(const std::string& name) const {
        auto it = tools_.find(name);
        return it == tools_.end() ? nullptr : it->second.get();
    }

    // OpenAI-style tool specs for the names allowed this turn (empty = all).
    nlohmann::json specs(const std::vector<std::string>& allow = {}) const;

    std::vector<std::string> names() const;

private:
    std::map<std::string, std::shared_ptr<ITool>> tools_;
};

// Registers all built-in tools (web, docs, print, shopping, reminders, memory,
// camera, queue_deep_task). Implemented in agent/tools/register_tools.cpp.
void registerBuiltinTools(ToolRegistry& reg);

} // namespace polymath
