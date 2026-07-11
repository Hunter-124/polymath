#pragma once
//
// ui_control — compose the on-screen layout via the EventBus. Worker thread →
// publishSurfaceRequest / publishNavigateRequest / publishWindowRequest (same
// pattern as camera_tools). Registration owned by C5.
//
// Schema v2 (overhaul2 A3, docs/overhaul2/01_DAG.md §A3):
//   spawn_surface | close_surface | arrange  -> SurfaceRequest (extended args)
//   open_page                                -> NavigateRequest
//   window                                   -> WindowRequest
// QML-side handlers for open_page/window land in E4; this tool only
// publishes/relays the schema. See docs/overhaul2/results/A3_notes.md for the
// full payload/signal contract and the canonical page-id list.
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
