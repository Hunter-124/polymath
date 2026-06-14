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

// describe_camera — ask the local vision model about a camera's CURRENT view
// (free-form question, or a plain description). Round-trips to VisionService over
// the EventBus (it owns the live frames + the VLM) and waits for the answer.
class DescribeCameraTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
