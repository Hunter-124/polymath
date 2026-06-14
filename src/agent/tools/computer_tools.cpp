#include "computer_tools.h"
#include "desktop_controller.h"
#include "logging.h"

namespace polymath {

using nlohmann::json;

// --- look_at_screen ---------------------------------------------------------
std::string ScreenLookTool::name() const { return "look_at_screen"; }
std::string ScreenLookTool::description() const {
    return "Capture the current screen and describe what is on it (windows, buttons, "
           "fields, menus, visible text) using the local vision model. Use this FIRST "
           "to see the screen before acting, and again after an action to confirm the "
           "result before the next step.";
}
nlohmann::json ScreenLookTool::parametersSchema() const {
    return {{"type", "object"},
            {"properties", {
                {"question", {{"type", "string"},
                              {"description", "Optional: ask about something specific on screen"}}}}}};
}
ToolResult ScreenLookTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    if (!ctx.inference)
        return {false, {{"error", "no vision model"}}, "look_at_screen: vision unavailable"};
    const std::string q = args.is_object() ? args.value("question", std::string{}) : std::string{};
    const std::string desc = DesktopController::describe(*ctx.inference, q);
    return {true, {{"screen", desc}}, "looked at the screen"};
}

// --- computer_click ---------------------------------------------------------
std::string ComputerClickTool::name() const { return "computer_click"; }
std::string ComputerClickTool::description() const {
    return "Click a UI element on screen. Give EITHER `target` (a short description of the "
           "element, e.g. \"the Save button\", \"Address bar\") — located precisely via "
           "Windows accessibility, falling back to the vision model — OR explicit `x_pct` "
           "and `y_pct` screen percentages (0-100 from the top-left). Optional `button` "
           "(left|right|middle) and `double` (true for a double-click). Click only what the "
           "task requires; never dismiss dialogs, security prompts, or close windows on your "
           "own initiative.";
}
nlohmann::json ComputerClickTool::parametersSchema() const {
    return {{"type", "object"},
            {"properties", {
                {"target", {{"type", "string"},  {"description", "Description of the element to click"}}},
                {"x_pct",  {{"type", "number"},  {"description", "Horizontal % from left (0-100)"}}},
                {"y_pct",  {{"type", "number"},  {"description", "Vertical % from top (0-100)"}}},
                {"button", {{"type", "string"},  {"description", "left|right|middle (default left)"}}},
                {"double", {{"type", "boolean"}, {"description", "true for a double-click"}}}}}};
}
ToolResult ComputerClickTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    if (!args.is_object())
        return {false, {{"error", "object args required"}}, "computer_click: bad args"};
    const std::string button = args.value("button", std::string{"left"});
    const bool dbl = args.value("double", false);

    if (args.contains("x_pct") && args["x_pct"].is_number() &&
        args.contains("y_pct") && args["y_pct"].is_number()) {
        const double nx = args["x_pct"].get<double>() / 100.0;
        const double ny = args["y_pct"].get<double>() / 100.0;
        DesktopController::clickAt(nx, ny, button, dbl);
        return {true, {{"x_pct", nx * 100}, {"y_pct", ny * 100}}, "clicked at coordinates"};
    }

    const std::string target = args.value("target", std::string{});
    if (target.empty())
        return {false, {{"error", "need target or x_pct/y_pct"}}, "computer_click: missing target"};
    if (!ctx.inference)
        return {false, {{"error", "no vision model"}}, "computer_click: vision unavailable"};

    const bool ok = DesktopController::click(target, *ctx.inference, button, dbl);
    return {ok, {{"target", target}, {"clicked", ok}},
            ok ? ("clicked \"" + target + "\"") : ("could not locate \"" + target + "\"")};
}

// --- computer_type ----------------------------------------------------------
std::string ComputerTypeTool::name() const { return "computer_type"; }
std::string ComputerTypeTool::description() const {
    return "Type text at the current keyboard focus (click the target field first). Types "
           "the given text exactly. Do not type passwords or other secrets unless the user "
           "explicitly provided them for this task.";
}
nlohmann::json ComputerTypeTool::parametersSchema() const {
    return {{"type", "object"},
            {"properties", {{"text", {{"type", "string"}, {"description", "The text to type"}}}}},
            {"required", {"text"}}};
}
ToolResult ComputerTypeTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string text = args.is_object() ? args.value("text", std::string{}) : std::string{};
    if (text.empty())
        return {false, {{"error", "text required"}}, "computer_type: missing text"};
    DesktopController::type(text);
    return {true, {{"typed", text.size()}}, "typed text"};
}

// --- computer_key -----------------------------------------------------------
std::string ComputerKeyTool::name() const { return "computer_key"; }
std::string ComputerKeyTool::description() const {
    return "Press a key or key combination, e.g. \"enter\", \"tab\", \"esc\", \"ctrl+c\", "
           "\"ctrl+s\", \"alt+tab\", \"win+d\". Combine with '+'.";
}
nlohmann::json ComputerKeyTool::parametersSchema() const {
    return {{"type", "object"},
            {"properties", {{"keys", {{"type", "string"}, {"description", "Key chord, e.g. ctrl+c"}}}}},
            {"required", {"keys"}}};
}
ToolResult ComputerKeyTool::invoke(const nlohmann::json& args, ToolContext&) {
    const std::string keys = args.is_object() ? args.value("keys", std::string{}) : std::string{};
    if (keys.empty())
        return {false, {{"error", "keys required"}}, "computer_key: missing keys"};
    const bool ok = DesktopController::key(keys);
    return {ok, {{"keys", keys}, {"pressed", ok}},
            ok ? ("pressed " + keys) : ("unrecognized key chord: " + keys)};
}

// --- computer_scroll --------------------------------------------------------
std::string ComputerScrollTool::name() const { return "computer_scroll"; }
std::string ComputerScrollTool::description() const {
    return "Scroll the mouse wheel. Positive `amount` scrolls up, negative scrolls down "
           "(in wheel notches).";
}
nlohmann::json ComputerScrollTool::parametersSchema() const {
    return {{"type", "object"},
            {"properties", {{"amount", {{"type", "integer"},
                             {"description", "Notches: + up, - down"}}}}},
            {"required", {"amount"}}};
}
ToolResult ComputerScrollTool::invoke(const nlohmann::json& args, ToolContext&) {
    const int amount = args.is_object() ? args.value("amount", 0) : 0;
    if (amount == 0)
        return {false, {{"error", "amount required"}}, "computer_scroll: amount 0"};
    DesktopController::scroll(amount);
    return {true, {{"amount", amount}}, "scrolled"};
}

} // namespace polymath
