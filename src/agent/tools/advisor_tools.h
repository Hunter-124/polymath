#pragma once
//
// Wave Z advisor inputs: local calendar (.ics) and drop-folder inbox notes.
// No OAuth / cloud APIs — owner points at paths via settings.
//
#include "i_tool.h"

namespace polymath {

class CalendarReadTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class InboxNotesTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
