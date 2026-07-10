#pragma once
//
// Skill tools (overhaul 03 §4):
//   run_skill  — expand a named skill with params → goal (+ plan_steps when DB)
//   save_skill — AI-author a new skill.json (confirm forced true)
//
// Tool *classes* only — registration into ToolRegistry is owned by C5.
//
#include "i_tool.h"

namespace polymath {

class SkillRegistry;

class RunSkillTool : public ITool {
public:
    explicit RunSkillTool(SkillRegistry& registry);

    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;

private:
    SkillRegistry& registry_;
};

class SaveSkillTool : public ITool {
public:
    explicit SaveSkillTool(SkillRegistry& registry);

    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override;

private:
    SkillRegistry& registry_;
};

} // namespace polymath
