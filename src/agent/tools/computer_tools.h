#pragma once
//
// Computer-use tools — let the agent drive the desktop: see the screen (local VLM),
// click UI elements (Windows UI Automation first, vision-model fallback), type,
// press key chords, and scroll. Backed by src/desktop/DesktopController. Every
// action lights the on-screen "AI is driving" border and writes an activity-log
// line; a UI panic-stop aborts in-flight actions.
//
#include "i_tool.h"

namespace polymath {

class ScreenLookTool : public ITool {   // "look_at_screen"
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class ComputerClickTool : public ITool {  // "computer_click"
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class ComputerTypeTool : public ITool {   // "computer_type"
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class ComputerKeyTool : public ITool {    // "computer_key"
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

class ComputerScrollTool : public ITool { // "computer_scroll"
public:
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;
};

} // namespace polymath
