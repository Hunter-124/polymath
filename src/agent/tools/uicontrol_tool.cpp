#include "uicontrol_tool.h"
#include "event_bus.h"
#include "logging.h"

#include <QString>
#include <QUuid>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

// ui_control — publish SurfaceRequest/NavigateRequest/WindowRequest on the
// EventBus so SurfaceHost / Main.qml (QML, node E4) can spawn/close/arrange
// surfaces, navigate the nav rail, or drive the app window.
// Schema v2: docs/overhaul2/01_DAG.md §A3, contract notes in
// docs/overhaul2/results/A3_notes.md.

namespace polymath {

namespace {

// Canonical nav-rail page ids (lowercase snake_case — small-model-friendly).
// Must track Main.qml's `pages` list (name field); E4 maps id -> display name
// via window.goToPage(). Aliases let a small model use natural phrasing
// ("home", "camera", "settings page", "Mobile Access") and still normalize to
// the fixed id.
const std::vector<std::pair<std::string, std::string>>& pageAliases() {
    static const std::vector<std::pair<std::string, std::string>> table = {
        {"dashboard", "dashboard"},       {"home", "dashboard"},
        {"chat", "chat"},                 {"assistant", "chat"},
        {"cameras", "cameras"},           {"camera", "cameras"},
        {"timeline", "timeline"},         {"history", "timeline"},
        {"tasks", "tasks"},               {"taskqueue", "tasks"},
        {"task_queue", "tasks"},
        {"shopping", "shopping"},         {"shopping_list", "shopping"},
        {"agents", "agents"},             {"agent_sessions", "agents"},
        {"personalities", "personalities"}, {"personality", "personalities"},
        {"models", "models"},             {"model_manager", "models"},
        {"privacy", "privacy"},
        {"mobile_access", "mobile_access"}, {"mobile", "mobile_access"},
        {"settings", "settings"},         {"settings_page", "settings"},
    };
    return table;
}

// Lowercase + spaces/dashes -> underscores, then resolve through the alias
// table. Unknown ids pass through unchanged (caller/QML side decides).
std::string normalizePageId(const std::string& raw) {
    std::string s;
    s.reserve(raw.size());
    for (char c : raw) {
        if (c == ' ' || c == '-')
            s.push_back('_');
        else
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    for (const auto& [alias, canonical] : pageAliases())
        if (alias == s) return canonical;
    return s;
}

const std::vector<std::string>& windowVerbs() {
    static const std::vector<std::string> verbs = {
        "present", "fullscreen", "restore", "always_on_top",
        "normal", "raise", "hide_to_tray"};
    return verbs;
}

} // namespace

std::string UiControlTool::name() const { return "ui_control"; }

std::string UiControlTool::description() const {
    return
        "Control the on-screen layout: navigate the nav rail, spawn/close/arrange "
        "floating glass surfaces (embedded web pages, YouTube players, images, notes), "
        "and drive the app window (fullscreen/present/restore/...). "
        "THIS is how you show websites and videos to the user — they render inside "
        "Polymath content cards that share the menu styling. Do NOT use browser_drive "
        "or app_launch/Chrome to display pages for the user.\n"
        "\n"
        "actions:\n"
        "  open_page      args: {page}. Switch the main view to a nav-rail page.\n"
        "  spawn_surface  args: {type, id?, title?, caption?, md?, x?, y?, w?, h?, "
        "group?, args?}. Open a floating glass card. type: placeholder|image|web|"
        "video|video_picker|note|monitor. group tags surfaces that belong together "
        "(a research board). x/y/w/h are optional layout hints (pixels).\n"
        "  close_surface  args: {id}. Remove a surface.\n"
        "  arrange        args: {layout}. layout: tile|stack|split-left|split-right|"
        "full|board.\n"
        "  window         args: {verb}. verb: present|fullscreen|restore|"
        "always_on_top|normal|raise|hide_to_tray. present = raise + activate + show "
        "surfaces — the \"AI takes over the screen to show you something\" verb.\n"
        "\n"
        "surface args by type:\n"
        "  web   → args.url (required, http(s)), optional args.title\n"
        "  video → args.videoId (preferred) and/or args.url (YouTube watch URL); "
        "ALWAYS call youtube_search first, then spawn with the chosen videoId — "
        "never open youtube.com as type=web. Plays in-app with adblock.\n"
        "  video_picker → args.results (array from youtube_search)\n"
        "  image → args.url or args.path; optional caption\n"
        "  note  → md (markdown body) and/or caption\n"
        "\n"
        "examples:\n"
        "  \"open wikipedia about mars\" -> {\"action\":\"spawn_surface\","
        "\"type\":\"web\",\"title\":\"Mars\",\"args\":{\"url\":\"https://en.wikipedia.org/wiki/Mars\"}}\n"
        "  \"pull up a video about castles\" -> {\"action\":\"spawn_surface\","
        "\"type\":\"video\",\"title\":\"Castles\",\"args\":{\"videoId\":\"...\"}}\n"
        "  \"go to settings\" -> {\"action\":\"open_page\",\"page\":\"settings\"}\n"
        "  \"take over the screen and show me\" -> {\"action\":\"window\","
        "\"verb\":\"present\"}\n"
        "  \"close that\" -> {\"action\":\"close_surface\",\"id\":\"<surface id>\"}\n"
        "\n"
        "page ids: dashboard|chat|cameras|timeline|tasks|shopping|agents|"
        "personalities|models|privacy|mobile_access|settings.";
}

nlohmann::json UiControlTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"action", {{"type", "string"},
                        {"enum", nlohmann::json::array(
                            {"open_page", "spawn_surface", "close_surface",
                             "arrange", "window"})},
                        {"description", "What to do to the layout"}}},
            {"page",  {{"type", "string"},
                       {"enum", nlohmann::json::array(
                           {"dashboard", "chat", "cameras", "timeline", "tasks",
                            "shopping", "agents", "personalities", "models",
                            "privacy", "mobile_access", "settings"})},
                       {"description",
                        "Page id for open_page (nav-rail display names like "
                        "\"Settings\" are also accepted and normalized)"}}},
            {"id",    {{"type", "string"},
                       {"description", "Surface id (close/arrange; auto-generated on spawn)"}}},
            {"type",  {{"type", "string"},
                       {"enum", nlohmann::json::array(
                           {"placeholder", "image", "web", "video", "video_picker",
                            "note", "monitor"})},
                       {"description", "Surface kind for spawn_surface. "
                                       "web/video embed inside the app (not a "
                                       "separate browser window)."}}},
            {"title", {{"type", "string"},
                       {"description", "Surface title"}}},
            {"caption", {{"type", "string"},
                       {"description", "Optional subtitle/caption shown under the title "
                                       "(e.g. image caption)"}}},
            {"md",    {{"type", "string"},
                       {"description", "Optional markdown body for a text/note card"}}},
            {"x",     {{"type", "number"}, {"description", "Optional x position hint"}}},
            {"y",     {{"type", "number"}, {"description", "Optional y position hint"}}},
            {"w",     {{"type", "number"}, {"description", "Optional width hint"}}},
            {"h",     {{"type", "number"}, {"description", "Optional height hint"}}},
            {"group", {{"type", "string"},
                       {"description", "Research-board grouping key; surfaces sharing "
                                       "a group render together"}}},
            {"args",  {{"type", "object"},
                       {"description", "Surface-specific args (url, path, mode, …)"}}},
            {"layout",{{"type", "string"},
                       {"enum", nlohmann::json::array(
                           {"tile", "stack", "split-left", "split-right", "full",
                            "board"})},
                       {"description", "Layout for arrange"}}},
            {"verb",  {{"type", "string"},
                       {"enum", nlohmann::json(windowVerbs())},
                       {"description", "Window verb for the window action"}}},
        }},
        {"required", {"action"}},
    };
}

ToolResult UiControlTool::invoke(const nlohmann::json& args, ToolContext& /*ctx*/) {
    const std::string action = args.value("action", "");
    if (action.empty())
        return {false, {{"error", "action required"}}, "ui_control: missing action"};

    if (action == "open_page") {
        const std::string rawPage = args.value("page", args.value("title", ""));
        if (rawPage.empty())
            return {false, {{"error", "page required for open_page"}},
                    "ui_control: missing page"};
        const std::string page = normalizePageId(rawPage);

        NavigateRequest n;
        n.page = QString::fromStdString(page);
        nlohmann::json a = nlohmann::json::object();
        if (args.contains("args") && args["args"].is_object())
            a = args["args"];
        n.args_json = QString::fromStdString(a.dump());

        EventBus::instance().publishNavigateRequest(n);
        PM_INFO("ui_control: open_page page={}", page);

        nlohmann::json content = {{"action", "open_page"}, {"page", page}, {"published", true}};
        return {true, std::move(content), "ui_control open_page page=" + page};
    }

    if (action == "window") {
        const std::string verb = args.value("verb", "");
        const auto& verbs = windowVerbs();
        if (std::find(verbs.begin(), verbs.end(), verb) == verbs.end()) {
            nlohmann::json allowed = nlohmann::json::array();
            for (const auto& v : verbs) allowed.push_back(v);
            return {false,
                    {{"error", "verb required for window (one of: present|fullscreen|"
                                "restore|always_on_top|normal|raise|hide_to_tray)"},
                     {"allowed", allowed}},
                    "ui_control: bad verb"};
        }

        WindowRequest w;
        w.verb = QString::fromStdString(verb);
        EventBus::instance().publishWindowRequest(w);
        PM_INFO("ui_control: window verb={}", verb);

        nlohmann::json content = {{"action", "window"}, {"verb", verb}, {"published", true}};
        return {true, std::move(content), "ui_control window verb=" + verb};
    }

    // spawn_surface | close_surface | arrange -> SurfaceRequest.
    SurfaceRequest r;
    if (action == "spawn_surface") {
        r.action = QStringLiteral("spawn");
        std::string type = args.value("type", "placeholder");
        // Aliases models often invent when they mean "embedded page".
        if (type == "browser" || type == "webpage" || type == "website" ||
            type == "page" || type == "url")
            type = "web";
        if (type == "youtube" || type == "yt" || type == "player")
            type = "video";
        if (type == "picker" || type == "results")
            type = "video_picker";
        if (type == "text" || type == "markdown" || type == "card")
            type = "note";
        r.type = QString::fromStdString(type);
        r.title = QString::fromStdString(args.value("title", "Surface"));
        std::string id = args.value("id", "");
        if (id.empty())
            id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        r.id = QString::fromStdString(id);

        nlohmann::json surfaceArgs = nlohmann::json::object();
        if (args.contains("args") && args["args"].is_object())
            surfaceArgs = args["args"];
        // Promote top-level url/videoId if the model put them outside args{}.
        if (!surfaceArgs.contains("url") && args.contains("url") && args["url"].is_string())
            surfaceArgs["url"] = args["url"];
        if (!surfaceArgs.contains("videoId") && args.contains("videoId") &&
            args["videoId"].is_string())
            surfaceArgs["videoId"] = args["videoId"];
        if (!surfaceArgs.contains("results") && args.contains("results"))
            surfaceArgs["results"] = args["results"];
        // web without url but with a title that looks like a URL — promote it.
        if ((type == "web" || type == "video") &&
            (!surfaceArgs.contains("url") || surfaceArgs["url"].get<std::string>().empty())) {
            const std::string titleGuess = args.value("title", "");
            if (titleGuess.rfind("http://", 0) == 0 || titleGuess.rfind("https://", 0) == 0)
                surfaceArgs["url"] = titleGuess;
        }
        if (type == "web" &&
            (!surfaceArgs.contains("url") ||
             !surfaceArgs["url"].is_string() ||
             surfaceArgs["url"].get<std::string>().empty())) {
            return {false,
                    {{"error", "spawn_surface type=web requires args.url (http/https)"},
                     {"hint", "Embed pages with ui_control; do not open an external browser."}},
                    "ui_control: web surface missing url"};
        }
        r.args_json = QString::fromStdString(surfaceArgs.dump());

        // A3: optional extended spawn args (backward compatible — all default-empty).
        r.caption = QString::fromStdString(args.value("caption", ""));
        r.md = QString::fromStdString(args.value("md", ""));
        r.group = QString::fromStdString(args.value("group", ""));
        if (args.contains("x") && args["x"].is_number()) r.x = args["x"].get<double>();
        if (args.contains("y") && args["y"].is_number()) r.y = args["y"].get<double>();
        if (args.contains("w") && args["w"].is_number()) r.w = args["w"].get<double>();
        if (args.contains("h") && args["h"].is_number()) r.h = args["h"].get<double>();
    } else if (action == "close_surface") {
        r.action = QStringLiteral("close");
        r.id = QString::fromStdString(args.value("id", ""));
        if (r.id.isEmpty())
            return {false, {{"error", "id required for close_surface"}},
                    "ui_control: missing id"};
        r.args_json = QStringLiteral("{}");
    } else if (action == "arrange") {
        r.action = QStringLiteral("arrange");
        r.id = QString::fromStdString(args.value("id", ""));
        // SurfaceHost.arrange reads the layout name from `type` (see
        // SurfaceHost.qml onSurfaceRequested). Also mirror into args_json.
        const std::string layout = args.value("layout", "tile");
        r.type = QString::fromStdString(layout);
        nlohmann::json a = nlohmann::json::object();
        a["layout"] = layout;
        if (args.contains("args") && args["args"].is_object()) {
            for (auto it = args["args"].begin(); it != args["args"].end(); ++it)
                a[it.key()] = it.value();
        }
        r.args_json = QString::fromStdString(a.dump());
    } else {
        return {false,
                {{"error", "unknown action: " + action},
                 {"allowed", nlohmann::json::array(
                     {"open_page", "spawn_surface", "close_surface", "arrange", "window"})}},
                "ui_control: bad action"};
    }

    EventBus::instance().publishSurfaceRequest(r);
    PM_INFO("ui_control: action={} id={} type={} title={}",
            r.action.toStdString(), r.id.toStdString(),
            r.type.toStdString(), r.title.toStdString());

    nlohmann::json content = {
        {"action", r.action.toStdString()},
        {"id", r.id.toStdString()},
        {"type", r.type.toStdString()},
        {"title", r.title.toStdString()},
        {"args_json", r.args_json.toStdString()},
        {"published", true},
    };
    return {true, std::move(content),
            "ui_control " + r.action.toStdString() +
                (r.id.isEmpty() ? "" : (" id=" + r.id.toStdString()))};
}

} // namespace polymath
