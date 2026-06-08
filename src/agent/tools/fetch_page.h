#pragma once
//
// fetch_page — fetch a URL and return readable extracted text. Implementation in
// fetch_page.cpp.
//
#include "i_tool.h"

namespace polymath {

class FetchPageTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
