#pragma once
//
// queue_deep_task — push a heavy background job (research, report, summary) into
// the `tasks` queue for the TaskScheduler to run when the machine is idle.
// Implementation in queue_tool.cpp.
//
#include "i_tool.h"

namespace polymath {

class QueueDeepTaskTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
