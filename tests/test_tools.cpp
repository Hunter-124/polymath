// Smoke test for the tool registry + the reference shopping tools.
#include "tool_registry.h"
#include "database.h"
#include <cassert>
#include <cstdio>
#include <filesystem>

using namespace polymath;

int main() {
    auto tmp = std::filesystem::temp_directory_path() / "polymath_test_tools.db";
    std::filesystem::remove(tmp);
    Database db;
    assert(db.open(tmp.string()));

    ToolRegistry reg;
    registerBuiltinTools(reg);
    assert(reg.get("shopping_add") != nullptr);
    assert(reg.get("shopping_list") != nullptr);

    // specs() produces valid OpenAI-style tool descriptions
    auto specs = reg.specs();
    assert(specs.is_array() && !specs.empty());
    assert(specs[0]["function"].contains("parameters"));

    // invoke shopping_add then shopping_list
    ToolContext ctx; ctx.db = &db;
    auto add = reg.get("shopping_add")->invoke({{"item", "eggs"}}, ctx);
    assert(add.ok);
    auto list = reg.get("shopping_list")->invoke({}, ctx);
    assert(list.ok && list.content["items"].size() == 1);
    assert(list.content["items"][0]["item"] == "eggs");

    db.close();
    std::filesystem::remove(tmp);
    std::puts("test_tools: OK");
    return 0;
}
