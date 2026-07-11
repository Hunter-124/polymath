#pragma once
//
// Screen tools: screen_capture / screen_describe. Grab a monitor (or titled
// window) via Qt QScreen, save a PNG under data/captures/, and optionally run
// InferenceManager::describeImage for a vision caption. Privacy-gated by
// keys::ScreenCapture (privacy.screen_capture). Implementations in
// screen_tools.cpp. Registration is owned by C2/orchestrator (see
// docs/overhaul2/results/C3_register.md).
//
#include "i_tool.h"

namespace polymath {

class ScreenCaptureTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class ScreenDescribeTool : public ITool {
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
