#pragma once
//
// web_search — query the configured search backend and return ranked results.
// Implementation in web_search.cpp.
//
#include "i_tool.h"

namespace polymath {

class WebSearchTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
