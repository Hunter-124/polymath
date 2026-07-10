#pragma once
//
// ui_control — compose the on-screen layout via EventBus SurfaceRequest
// (overhaul 02 §F5). Worker thread → publishSurfaceRequest (same pattern as
// camera_tools). Registration owned by C5.
//
#include "i_tool.h"

namespace polymath {

class UiControlTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
