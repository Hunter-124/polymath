#pragma once
//
// set_reminder — persist a reminder into the `reminders` table for the
// ProactiveEngine to fire. Implementation in reminders.cpp.
//
#include "i_tool.h"

namespace polymath {

class SetReminderTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
