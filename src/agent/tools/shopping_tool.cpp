#include "shopping_tool.h"
#include "database.h"
#include "types.h"

// Reference implementation of the ITool pattern — fully working, no native deps.
// registerBuiltinTools() now lives in register_tools.cpp; this file keeps only
// the shopping-list tool classes.

namespace polymath {

// --- shopping_add -----------------------------------------------------------

std::string ShoppingAddTool::name() const { return "shopping_add"; }
std::string ShoppingAddTool::description() const { return "Add an item to the shopping list."; }

nlohmann::json ShoppingAddTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"item",     {{"type", "string"}, {"description", "Item to add"}}},
            {"quantity", {{"type", "string"}, {"description", "Optional quantity"}}},
        }},
        {"required", {"item"}},
    };
}

ToolResult ShoppingAddTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    std::string item = args.value("item", "");
    std::string qty  = args.value("quantity", "");
    if (item.empty()) return {false, {{"error", "item required"}}, "shopping_add: missing item"};
    ctx.db->exec("INSERT INTO shopping_items(item,quantity,created_at) VALUES(?1,?2,?3)",
                 {item, qty, to_unix(Clock::now())});
    return {true, {{"added", item}}, "Added " + item + " to shopping list"};
}

// --- shopping_list ----------------------------------------------------------

std::string ShoppingListTool::name() const { return "shopping_list"; }
std::string ShoppingListTool::description() const { return "List current shopping-list items."; }

nlohmann::json ShoppingListTool::parametersSchema() const {
    return {{"type", "object"}, {"properties", nlohmann::json::object()}};
}

ToolResult ShoppingListTool::invoke(const nlohmann::json&, ToolContext& ctx) {
    nlohmann::json items = nlohmann::json::array();
    ctx.db->query("SELECT item,quantity FROM shopping_items WHERE done=0 ORDER BY created_at",
                  {}, [&](const Row& r) {
                      items.push_back({{"item", r.text(0)}, {"quantity", r.text(1)}});
                  });
    return {true, {{"items", items}}, "Listed shopping items"};
}

// --- shopping_remove --------------------------------------------------------

std::string ShoppingRemoveTool::name() const { return "shopping_remove"; }
std::string ShoppingRemoveTool::description() const {
    return "Mark a shopping-list item as done (removes it from the active list).";
}

nlohmann::json ShoppingRemoveTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"item", {{"type", "string"}, {"description", "Item to remove (case-insensitive match)"}}},
        }},
        {"required", {"item"}},
    };
}

ToolResult ShoppingRemoveTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    std::string item = args.value("item", "");
    if (item.empty()) return {false, {{"error", "item required"}}, "shopping_remove: missing item"};
    // Mark the matching active rows done (LOWER for case-insensitive matching).
    ctx.db->exec("UPDATE shopping_items SET done=1 "
                 "WHERE done=0 AND LOWER(item)=LOWER(?1)", {item});
    return {true, {{"removed", item}}, "Removed " + item + " from shopping list"};
}

} // namespace polymath
