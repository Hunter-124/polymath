// Unit tests for SkillRegistry / skill load-validate-expand (overhaul C3 / 03 §4).
// Deterministic; no models required. Covers:
//   * validateSkillJson accept / reject
//   * {param} substitution in strings and nested JSON
//   * expandSkillToGoal required-param checks
//   * SkillRegistry load of starter skills + expand of each
//   * save_skill force-confirm + RunSkillTool goal persistence
#include "skills/skill.h"
#include "skills/skill_registry.h"
#include "tools/skill_tools.h"
#include "database.h"
#include "paths.h"

#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <QCoreApplication>

using namespace polymath;
namespace fs = std::filesystem;

namespace {

fs::path starterSkillsDir() {
#ifdef POLYMATH_SKILLS_DIR
    return fs::path(POLYMATH_SKILLS_DIR);
#else
    // Fallback: walk up from cwd looking for data/skills/slop_mode/skill.json
    fs::path cur = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        auto cand = cur / "data" / "skills";
        if (fs::exists(cand / "slop_mode" / "skill.json")) return cand;
        if (!cur.has_parent_path() || cur.parent_path() == cur) break;
        cur = cur.parent_path();
    }
    return {};
#endif
}

fs::path makeTempDir(const char* tag) {
    auto base = fs::temp_directory_path() / (std::string("polymath_test_skills_") + tag);
    fs::remove_all(base);
    fs::create_directories(base);
    return base;
}

void writeSkill(const fs::path& dir, const std::string& name, const nlohmann::json& j) {
    auto d = dir / name;
    fs::create_directories(d);
    std::ofstream out(d / "skill.json");
    out << j.dump(2);
}

void test_validate_ok() {
    nlohmann::json j = {
        {"name", "demo"},
        {"description", "A demo skill"},
        {"triggers", {"demo please"}},
        {"params", {{"type", "object"},
                    {"properties", {{"x", {{"type", "string"}}}}},
                    {"required", {"x"}}}},
        {"confirm", false},
        {"steps", nlohmann::json::array({
            {{"kind", "tool"}, {"tool", "web_search"},
             {"args", {{"query", "{x}"}}},
             {"description", "Search for {x}"}},
            {{"kind", "prompt"}, {"description", "Summarize"}},
        })},
    };
    auto v = validateSkillJson(j);
    assert(v.ok);
    assert(v.skill.name == "demo");
    assert(v.skill.steps.size() == 2);
    assert(v.skill.steps[0].kind == "tool");
    assert(v.skill.steps[0].tool == "web_search");
    std::puts("  validate_ok");
}

void test_validate_rejects() {
    // Missing steps
    {
        auto v = validateSkillJson({{"name", "x"}, {"description", "d"}});
        assert(!v.ok);
    }
    // Bad name
    {
        auto v = validateSkillJson({
            {"name", "Bad Name!"},
            {"description", "d"},
            {"steps", nlohmann::json::array({{{"kind", "prompt"}, {"description", "p"}}})},
        });
        assert(!v.ok);
    }
    // Bad kind
    {
        auto v = validateSkillJson({
            {"name", "x"},
            {"description", "d"},
            {"steps", nlohmann::json::array({{{"kind", "magic"}, {"description", "p"}}})},
        });
        assert(!v.ok);
    }
    // tool without tool name
    {
        auto v = validateSkillJson({
            {"name", "x"},
            {"description", "d"},
            {"steps", nlohmann::json::array({{{"kind", "tool"}, {"description", "p"}}})},
        });
        assert(!v.ok);
    }
    std::puts("  validate_rejects");
}

void test_param_substitution() {
    nlohmann::json params = {{"topic", "cats"}, {"n", 3}, {"flag", true}};
    assert(substituteParams("hello {topic}!", params) == "hello cats!");
    assert(substituteParams("count={n} flag={flag}", params) == "count=3 flag=true");
    assert(substituteParams("missing {nope}", params) == "missing {nope}");
    assert(substituteParams("no placeholders", params) == "no placeholders");

    nlohmann::json nested = {
        {"url", "https://youtube.com/results?q={topic}"},
        {"meta", {{"title", "{topic} video"}, {"n", "{n}"}}},
        {"tags", nlohmann::json::array({"{topic}", "fixed"})},
    };
    auto out = substituteParamsJson(nested, params);
    assert(out["url"] == "https://youtube.com/results?q=cats");
    assert(out["meta"]["title"] == "cats video");
    assert(out["meta"]["n"] == "3");
    assert(out["tags"][0] == "cats");
    assert(out["tags"][1] == "fixed");
    std::puts("  param_substitution");
}

void test_expand_goal() {
    Skill s;
    s.name = "slop_mode";
    s.description = "Ambient YouTube for {topic}";
    s.params = {{"type", "object"},
                {"properties", {{"topic", {{"type", "string"}}}}},
                {"required", {"topic"}}};
    s.confirm = false;
    s.steps.push_back(SkillStep{
        "surface", "", "Spawn YouTube for {topic}",
        nlohmann::json{
            {"action", "spawn"},
            {"args", {{"url", "https://youtube.com/results?q={topic}"}}},
        }});
    s.steps.push_back(SkillStep{
        "tool", "agent_watch", "Watch agents",
        nlohmann::json{{"notify", "voice"}}});

    // Missing required param
    auto bad = expandSkillToGoal(s, nlohmann::json::object());
    assert(bad.contains("error"));

    auto goal = expandSkillToGoal(s, {{"topic", "lofi"}});
    assert(!goal.contains("error"));
    assert(goal["origin"] == "skill");
    assert(goal["confirm"] == false);
    assert(goal["context"]["skill"] == "slop_mode");
    assert(goal["steps"].size() == 2);
    assert(goal["steps"][0]["kind"] == "surface");
    assert(goal["steps"][0]["args"]["args"]["url"] ==
           "https://youtube.com/results?q=lofi");
    assert(goal["steps"][0]["description"] == "Spawn YouTube for lofi");
    assert(goal["steps"][1]["tool"] == "agent_watch");
    // Title gets param substitution too
    assert(goal["title"].get<std::string>().find("lofi") != std::string::npos);
    std::puts("  expand_goal");
}

void test_registry_load_starters() {
    const fs::path starters = starterSkillsDir();
    assert(!starters.empty() && "starter skills dir not found");
    assert(fs::exists(starters / "slop_mode" / "skill.json"));
    assert(fs::exists(starters / "morning_brief" / "skill.json"));
    assert(fs::exists(starters / "research_brief" / "skill.json"));

    SkillRegistry reg;
    reg.setSkillsDir(starters);
    reg.load();
    assert(reg.size() >= 3);
    assert(reg.has("slop_mode"));
    assert(reg.has("morning_brief"));
    assert(reg.has("research_brief"));

    auto cat = reg.catalog();
    assert(cat.is_array() && cat.size() >= 3);

    // slop_mode expand
    auto slop = reg.expand("slop_mode", {{"topic", "jazz"}});
    assert(!slop.contains("error"));
    assert(slop["steps"].size() == 2);
    assert(slop["steps"][0]["kind"] == "surface");
    const std::string url =
        slop["steps"][0]["args"]["args"]["url"].get<std::string>();
    assert(url.find("jazz") != std::string::npos);
    assert(slop["steps"][1]["tool"] == "agent_watch");

    // morning_brief: 3 steps, no required params
    auto morn = reg.expand("morning_brief", nlohmann::json::object());
    assert(!morn.contains("error"));
    assert(morn["steps"].size() == 3);
    assert(morn["steps"][0]["tool"] == "search_memory");
    assert(morn["steps"][1]["tool"] == "recall");
    assert(morn["steps"][2]["kind"] == "prompt");

    // research_brief: web_search x3 + prompts + draft_document
    auto res = reg.expand("research_brief", {{"topic", "fusion"}, {"angle", "home use"}});
    assert(!res.contains("error"));
    assert(res["steps"].size() == 6);
    int web_count = 0;
    for (const auto& st : res["steps"]) {
        if (st.value("tool", "") == "web_search") ++web_count;
    }
    assert(web_count == 3);
    assert(res["steps"].back()["tool"] == "draft_document");
    assert(res["steps"].back()["args"]["title"].get<std::string>().find("fusion")
           != std::string::npos);

    // research_brief missing topic
    auto miss = reg.expand("research_brief", nlohmann::json::object());
    assert(miss.contains("error"));

    // unknown skill
    auto unk = reg.expand("nope", {});
    assert(unk.contains("error"));

    std::puts("  registry_load_starters");
}

void test_save_and_run_tools() {
    auto tmp = makeTempDir("tools");
    auto dbpath = tmp / "t.db";
    Database db;
    assert(db.open(dbpath.string()));

    // Seed one skill, then save another via SaveSkillTool.
    writeSkill(tmp / "skills", "echo_skill", {
        {"name", "echo_skill"},
        {"description", "Echo {msg}"},
        {"params", {{"type", "object"},
                    {"properties", {{"msg", {{"type", "string"}}}}},
                    {"required", {"msg"}}}},
        {"confirm", false},
        {"steps", nlohmann::json::array({
            {{"kind", "prompt"}, {"description", "Say: {msg}"}},
        })},
    });

    SkillRegistry reg;
    reg.setSkillsDir(tmp / "skills");
    reg.load();
    assert(reg.has("echo_skill"));

    SaveSkillTool save(reg);
    ToolContext ctx;
    ctx.db = &db;

    auto saved = save.invoke({
        {"name", "ai_authored"},
        {"description", "AI-made skill for {thing}"},
        {"triggers", nlohmann::json::array({"do the thing"})},
        {"params", {{"type", "object"},
                    {"properties", {{"thing", {{"type", "string"}}}}},
                    {"required", {"thing"}}}},
        {"steps", nlohmann::json::array({
            {{"kind", "tool"}, {"tool", "web_search"},
             {"args", {{"query", "{thing}"}}},
             {"description", "Search {thing}"}},
        })},
        {"confirm", false},  // AI request — must still force true
    }, ctx);
    assert(saved.ok);
    assert(saved.content["confirm"] == true);
    assert(reg.has("ai_authored"));
    assert(reg.get("ai_authored")->confirm == true);
    assert(fs::exists(tmp / "skills" / "ai_authored" / "skill.json"));

    // run_skill persists goal + plan_steps
    RunSkillTool run(reg);
    auto ran = run.invoke({
        {"name", "echo_skill"},
        {"params", {{"msg", "hello"}}},
    }, ctx);
    assert(ran.ok);
    assert(ran.content.contains("goal_id"));
    assert(ran.content["status"] == "active");
    assert(ran.content["steps"].size() == 1);
    assert(ran.content["steps"][0]["description"] == "Say: hello");

    int64_t goal_id = ran.content["goal_id"].get<int64_t>();
    int step_count = 0;
    std::string step_desc;
    db.query("SELECT COUNT(*) FROM plan_steps WHERE goal_id=?1", {goal_id},
             [&](const Row& r) { step_count = static_cast<int>(r.i64(0)); });
    assert(step_count == 1);
    db.query("SELECT description FROM plan_steps WHERE goal_id=?1", {goal_id},
             [&](const Row& r) { step_desc = r.text(0); });
    assert(step_desc == "Say: hello");

    // confirm skill → waiting_user
    auto conf = run.invoke({
        {"name", "ai_authored"},
        {"params", {{"thing", "batteries"}}},
    }, ctx);
    assert(conf.ok);
    assert(conf.content["status"] == "waiting_user");
    assert(conf.content["confirm"] == true);

    db.close();
    fs::remove_all(tmp);
    std::puts("  save_and_run_tools");
}

void test_load_skill_file() {
    auto tmp = makeTempDir("file");
    writeSkill(tmp, "ok", {
        {"name", "ok"},
        {"description", "fine"},
        {"steps", nlohmann::json::array({
            {{"kind", "prompt"}, {"description", "hi"}},
        })},
    });
    auto v = loadSkillFile(tmp / "ok" / "skill.json");
    assert(v.ok);
    assert(v.skill.source_path == tmp / "ok" / "skill.json");

    auto bad = loadSkillFile(tmp / "missing" / "skill.json");
    assert(!bad.ok);
    fs::remove_all(tmp);
    std::puts("  load_skill_file");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);  // QFileSystemWatcher / QObject needs this

    std::puts("test_skills:");
    test_validate_ok();
    test_validate_rejects();
    test_param_substitution();
    test_expand_goal();
    test_load_skill_file();
    test_registry_load_starters();
    test_save_and_run_tools();
    std::puts("test_skills: OK");
    return 0;
}
