#include "tool_registry.h"
#include <algorithm>

namespace polymath {

void ToolRegistry::add(std::shared_ptr<ITool> tool, ToolRiskClass risk) {
    if (!tool) return;
    const std::string n = tool->name();
    tools_[n] = std::move(tool);
    risks_[n] = risk;
}

ToolRiskClass ToolRegistry::riskOf(const std::string& name) const {
    auto it = risks_.find(name);
    return it == risks_.end() ? ToolRiskClass::Read : it->second;
}

std::vector<std::string> ToolRegistry::names() const {
    std::vector<std::string> out;
    out.reserve(tools_.size());
    for (auto& [n, _] : tools_) out.push_back(n);
    return out;
}

nlohmann::json ToolRegistry::specs(const std::vector<std::string>& allow) const {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& [name, tool] : tools_) {
        if (!allow.empty() && std::find(allow.begin(), allow.end(), name) == allow.end())
            continue;
        arr.push_back({
            {"type", "function"},
            {"function", {
                {"name", name},
                {"description", tool->description()},
                {"parameters", tool->parametersSchema()},
            }},
            {"risk", toolRiskClassName(riskOf(name))},
        });
    }
    return arr;
}

} // namespace polymath
