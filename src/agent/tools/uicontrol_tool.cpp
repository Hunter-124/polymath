#include "uicontrol_tool.h"
#include "event_bus.h"
#include "logging.h"

#include <QString>
#include <QUuid>

// ui_control — publish SurfaceRequest on EventBus so SurfaceHost (QML) can
// spawn/close/arrange surfaces or open a page. Spec: docs/overhaul/02 §F5.

namespace polymath {

std::string UiControlTool::name() const { return "ui_control"; }

std::string UiControlTool::description() const {
    return "Compose the on-screen layout: open pages, spawn/close/arrange "
           "surfaces (placeholder, image, web, video, monitor).";
}

nlohmann::json UiControlTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"action", {{"type", "string"},
                        {"enum", nlohmann::json::array(
                            {"open_page", "spawn_surface", "close_surface", "arrange"})},
                        {"description", "What to do to the layout"}}},
            {"page",  {{"type", "string"},
                       {"description", "Page name for open_page (e.g. Dashboard, Chat)"}}},
            {"id",    {{"type", "string"},
                       {"description", "Surface id (close/arrange; auto-generated on spawn)"}}},
            {"type",  {{"type", "string"},
                       {"enum", nlohmann::json::array(
                           {"placeholder", "image", "web", "video", "monitor"})},
                       {"description", "Surface kind for spawn_surface"}}},
            {"title", {{"type", "string"},
                       {"description", "Surface title"}}},
            {"args",  {{"type", "object"},
                       {"description", "Surface-specific args (url, path, mode, …)"}}},
            {"layout",{{"type", "string"},
                       {"enum", nlohmann::json::array(
                           {"tile", "stack", "split-left", "split-right", "full"})},
                       {"description", "Layout for arrange"}}},
        }},
        {"required", {"action"}},
    };
}

ToolResult UiControlTool::invoke(const nlohmann::json& args, ToolContext& /*ctx*/) {
    const std::string action = args.value("action", "");
    if (action.empty())
        return {false, {{"error", "action required"}}, "ui_control: missing action"};

    SurfaceRequest r;
    // Map tool action names → bus SurfaceRequest.action vocabulary.
    if (action == "spawn_surface") {
        r.action = QStringLiteral("spawn");
        r.type = QString::fromStdString(args.value("type", "placeholder"));
        r.title = QString::fromStdString(args.value("title", "Surface"));
        std::string id = args.value("id", "");
        if (id.empty())
            id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        r.id = QString::fromStdString(id);
        if (args.contains("args") && args["args"].is_object())
            r.args_json = QString::fromStdString(args["args"].dump());
        else
            r.args_json = QStringLiteral("{}");
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
        nlohmann::json a = nlohmann::json::object();
        if (args.contains("layout")) a["layout"] = args["layout"];
        if (args.contains("args") && args["args"].is_object()) {
            for (auto it = args["args"].begin(); it != args["args"].end(); ++it)
                a[it.key()] = it.value();
        }
        r.args_json = QString::fromStdString(a.dump());
    } else if (action == "open_page") {
        r.action = QStringLiteral("open_page");
        r.title = QString::fromStdString(args.value("page", args.value("title", "")));
        if (r.title.isEmpty())
            return {false, {{"error", "page required for open_page"}},
                    "ui_control: missing page"};
        r.id = QString::fromStdString(args.value("id", ""));
        nlohmann::json a = {{"page", r.title.toStdString()}};
        if (args.contains("args") && args["args"].is_object())
            a["args"] = args["args"];
        r.args_json = QString::fromStdString(a.dump());
    } else {
        return {false,
                {{"error", "unknown action: " + action},
                 {"allowed", nlohmann::json::array(
                     {"open_page", "spawn_surface", "close_surface", "arrange"})}},
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
