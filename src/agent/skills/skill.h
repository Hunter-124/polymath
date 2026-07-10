#pragma once
//
// Skill — declarative multi-step workflow module (overhaul 03 §4).
// Loaded from data/skills/<name>/skill.json; expanded by SkillRegistry into a
// goal-shaped plan with {param} substitution.
//
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace polymath {

// One step in a skill's pre-authored plan. Mirrors plan_steps.kind values.
struct SkillStep {
    std::string    kind;          // tool | prompt | skill | agent_session | surface
    std::string    tool;          // tool name when kind == "tool"
    std::string    description;   // human-readable / prompt text
    nlohmann::json args = nlohmann::json::object();
};

struct Skill {
    std::string              name;
    std::string              description;
    std::vector<std::string> triggers;
    nlohmann::json           params = nlohmann::json::object();  // JSON Schema for inputs
    bool                     confirm = false;                    // true → wait for user
    std::vector<SkillStep>   steps;
    std::filesystem::path    source_path;                        // skill.json path (if loaded)
};

struct SkillValidation {
    bool        ok = false;
    std::string error;
    Skill       skill;
};

// Allowed step kinds (plan_steps / 03 §2.2).
bool isValidSkillStepKind(const std::string& kind);

// Parse + validate a skill.json object. `fallback_name` used when "name" is absent
// (typically the parent directory name). Does not touch the filesystem.
SkillValidation validateSkillJson(const nlohmann::json& j,
                                  const std::string& fallback_name = {});

// Load and validate a skill.json file from disk.
SkillValidation loadSkillFile(const std::filesystem::path& path);

// Replace `{param}` placeholders (word characters) using `params` object values.
// Non-string JSON values are dump()'d. Missing keys leave the placeholder intact.
// Applied recursively to strings inside JSON values.
std::string    substituteParams(const std::string& text, const nlohmann::json& params);
nlohmann::json substituteParamsJson(const nlohmann::json& value,
                                    const nlohmann::json& params);

// Expand a validated skill + runtime params into a goal-shaped JSON object:
//   { title, origin:"skill", context:{skill,params,confirm}, steps:[{description,kind,tool,args}] }
// Strings in description/tool/args receive {param} substitution.
// Returns { "error": "..." } on failure (missing required param, etc.).
nlohmann::json expandSkillToGoal(const Skill& skill, const nlohmann::json& params);

// Compact catalog entry for the router prompt (no steps).
nlohmann::json skillCatalogEntry(const Skill& skill);

} // namespace polymath
