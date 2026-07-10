#include "skill_tools.h"
#include "skills/skill_registry.h"
#include "tool_support.h"
#include "database.h"
#include "logging.h"

// run_skill / save_skill — skill harness tools (03 §4). Classes are constructed
// with a SkillRegistry reference; C5 wires them into registerBuiltinTools.

namespace polymath {

// ---------------------------------------------------------------------------
//  run_skill
// ---------------------------------------------------------------------------

RunSkillTool::RunSkillTool(SkillRegistry& registry) : registry_(registry) {}

std::string RunSkillTool::name() const { return "run_skill"; }

std::string RunSkillTool::description() const {
    return "Run a named skill: expand its pre-authored steps with the given params "
           "and create a goal to execute them. Use the skill catalog for available names.";
}

nlohmann::json RunSkillTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"name",   {{"type", "string"},
                        {"description", "Skill name (e.g. slop_mode, morning_brief)"}}},
            {"params", {{"type", "object"},
                        {"description", "Parameter values matching the skill's params schema"}}},
        }},
        {"required", {"name"}},
    };
}

ToolResult RunSkillTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const std::string name = args.value("name", "");
    if (name.empty())
        return {false, {{"error", "name required"}}, "run_skill: missing name"};

    nlohmann::json params = args.value("params", nlohmann::json::object());
    if (!params.is_object()) params = nlohmann::json::object();

    nlohmann::json goal = registry_.expand(name, params);
    if (goal.contains("error")) {
        return {false, goal, "run_skill: " + goal["error"].get<std::string>()};
    }

    const bool confirm = goal.value("confirm", false);
    const std::string status = confirm ? "waiting_user" : "active";
    const std::string title  = goal.value("title", "Skill: " + name);
    const int64_t now = tool_support::nowUnix();

    nlohmann::json content = {
        {"skill", name},
        {"title", title},
        {"status", status},
        {"confirm", confirm},
        {"origin", "skill"},
        {"steps", goal.value("steps", nlohmann::json::array())},
        {"context", goal.value("context", nlohmann::json::object())},
    };

    // Persist as a goal + plan_steps when a database is available so the
    // harness (C2 AgentLoop) can resume/execute without re-expanding.
    if (ctx.db) {
        nlohmann::json context = goal.value("context", nlohmann::json::object());
        context["trace"] = nlohmann::json::array();

        const int64_t goal_id = ctx.db->exec(
            "INSERT INTO goals(title,status,origin,context_json,created_at,updated_at) "
            "VALUES(?1,?2,'skill',?3,?4,?4)",
            {title, status, context.dump(), now});

        const auto& steps = goal["steps"];
        for (size_t i = 0; i < steps.size(); ++i) {
            const auto& st = steps[i];
            const std::string kind = st.value("kind", "prompt");
            const std::string desc = st.value("description", kind + " step");
            const std::string tool = st.value("tool", "");
            const std::string args_json = st.contains("args") ? st["args"].dump() : "{}";
            ctx.db->exec(
                "INSERT INTO plan_steps(goal_id,idx,description,kind,tool,args_json,"
                "status,attempts,updated_at) "
                "VALUES(?1,?2,?3,?4,?5,?6,'pending',0,?7)",
                {goal_id, static_cast<int64_t>(i), desc, kind,
                 tool.empty() ? nlohmann::json(nullptr) : nlohmann::json(tool),
                 args_json, now});
        }

        content["goal_id"] = goal_id;
        PM_INFO("run_skill: skill={} goal_id={} status={} steps={}",
                name, goal_id, status, steps.size());
    } else {
        PM_INFO("run_skill: skill={} (no db — expanded only) steps={}",
                name, content["steps"].size());
    }

    std::string summary = "Expanded skill " + name + " → " +
                          std::to_string(content["steps"].size()) + " steps";
    if (confirm) summary += " (waiting for user confirm)";
    if (content.contains("goal_id"))
        summary += " [goal " + std::to_string(content["goal_id"].get<int64_t>()) + "]";

    return {true, std::move(content), std::move(summary)};
}

// ---------------------------------------------------------------------------
//  save_skill
// ---------------------------------------------------------------------------

SaveSkillTool::SaveSkillTool(SkillRegistry& registry) : registry_(registry) {}

std::string SaveSkillTool::name() const { return "save_skill"; }

std::string SaveSkillTool::description() const {
    return "Author a new skill from a described routine and save it under data/skills/. "
           "AI-authored skills always require user confirmation before running "
           "(confirm is forced true until the user edits the skill).";
}

nlohmann::json SaveSkillTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"name",        {{"type", "string"},
                             {"description", "snake_case skill name (directory key)"}}},
            {"description", {{"type", "string"},
                             {"description", "What the skill does (router catalog text)"}}},
            {"triggers",    {{"type", "array"},
                             {"items", {{"type", "string"}}},
                             {"description", "Optional natural-language trigger phrases"}}},
            {"params",      {{"type", "object"},
                             {"description", "JSON Schema for skill parameters"}}},
            {"steps",       {{"type", "array"},
                             {"description",
                              "Ordered steps: {kind, tool?, description?, args?}"},
                             {"items", {{"type", "object"}}}}},
            {"confirm",     {{"type", "boolean"},
                             {"description", "Ignored for AI-authored skills (forced true)"}}},
        }},
        {"required", {"name", "description", "steps"}},
    };
}

ToolResult SaveSkillTool::invoke(const nlohmann::json& args, ToolContext& /*ctx*/) {
    if (!args.is_object())
        return {false, {{"error", "args must be an object"}}, "save_skill: bad args"};

    // Build the skill document from tool args (pass through known fields).
    nlohmann::json skill = nlohmann::json::object();
    skill["name"] = args.value("name", "");
    skill["description"] = args.value("description", "");
    if (args.contains("triggers")) skill["triggers"] = args["triggers"];
    if (args.contains("params"))   skill["params"]   = args["params"];
    if (args.contains("steps"))    skill["steps"]    = args["steps"];
    // confirm is always forced true for AI-authored skills (03 §4).
    skill["confirm"] = true;

    std::string err;
    if (!registry_.saveSkill(std::move(skill), /*force_confirm=*/true, &err)) {
        return {false, {{"error", err}}, "save_skill: " + err};
    }

    const std::string name = args.value("name", "");
    nlohmann::json content = {
        {"name", name},
        {"confirm", true},
        {"saved", true},
        {"catalog", registry_.catalog()},
    };
    PM_INFO("save_skill: authored skill '{}' (confirm forced)", name);
    return {true, std::move(content),
            "Saved skill " + name + " (confirm=true until user edits)"};
}

} // namespace polymath
