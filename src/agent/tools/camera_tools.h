#pragma once
//
// Camera tools: camera_snapshot / who_is_home. Read recent vision events from
// the DB and/or signal the VisionService over the EventBus. Implementations in
// camera_tools.cpp.
//
#include "i_tool.h"

namespace polymath {

class CameraSnapshotTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class WhoIsHomeTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
