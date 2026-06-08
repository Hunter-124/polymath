#include "tool_registry.h"
#include "database.h"
#include "types.h"

// Reference implementation of the ITool pattern — fully working, no native deps.
// The Wave-2 agent adds the remaining tools (web_search, fetch_page,
// draft_document, generate_lab_report, print_document, set_reminder, remember,
// recall, camera_snapshot, who_is_home, queue_deep_task) following this shape.

namespace polymath {

class ShoppingAddTool : public ITool {
public:
    std::string name() const override { return "shopping_add"; }
    std::string description() const override { return "Add an item to the shopping list."; }
    nlohmann::json parametersSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"item",     {{"type", "string"}, {"description", "Item to add"}}},
                {"quantity", {{"type", "string"}, {"description", "Optional quantity"}}},
            }},
            {"required", {"item"}},
        };
    }
    ToolResult invoke(const nlohmann::json& args, ToolContext& ctx) override {
        std::string item = args.value("item", "");
        std::string qty  = args.value("quantity", "");
        if (item.empty()) return {false, {{"error", "item required"}}, "shopping_add: missing item"};
        ctx.db->exec("INSERT INTO shopping_items(item,quantity,created_at) VALUES(?1,?2,?3)",
                     {item, qty, to_unix(Clock::now())});
        return {true, {{"added", item}}, "Added " + item + " to shopping list"};
    }
};

class ShoppingListTool : public ITool {
public:
    std::string name() const override { return "shopping_list"; }
    std::string description() const override { return "List current shopping-list items."; }
    nlohmann::json parametersSchema() const override {
        return {{"type", "object"}, {"properties", nlohmann::json::object()}};
    }
    ToolResult invoke(const nlohmann::json&, ToolContext& ctx) override {
        nlohmann::json items = nlohmann::json::array();
        ctx.db->query("SELECT item,quantity FROM shopping_items WHERE done=0 ORDER BY created_at",
                      {}, [&](const Row& r) {
                          items.push_back({{"item", r.text(0)}, {"quantity", r.text(1)}});
                      });
        return {true, {{"items", items}}, "Listed shopping items"};
    }
};

// Wave-0 registration. The Wave-2 agent expands this to register every builtin.
void registerBuiltinTools(ToolRegistry& reg) {
    reg.add(std::make_shared<ShoppingAddTool>());
    reg.add(std::make_shared<ShoppingListTool>());
}

} // namespace polymath
