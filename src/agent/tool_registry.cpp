#include "tool_registry.h"
#include <algorithm>

namespace polymath {

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
        });
    }
    return arr;
}

} // namespace polymath
