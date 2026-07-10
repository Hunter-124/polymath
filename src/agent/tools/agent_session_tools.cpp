#include "agent_session_tools.h"
#include "tool_registry.h"
#include "event_bus.h"
#include "database.h"
#include "logging.h"
#include "tool_support.h"

#include <QMetaObject>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QThread>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <string>

// agent_* tools — thin ITool wrappers over AgentSessionService Q_INVOKABLEs
// (05 §4). Cross-thread: BlockingQueuedConnection when the service lives on
// another thread (same pattern as SessionsModel).

namespace polymath {

namespace {

QString invokeSpawn(QObject* svc, const QString& provider, const QString& cwd,
                    const QString& prompt, const QString& title) {
    QString id;
    const bool ok = QMetaObject::invokeMethod(
        svc, "spawn",
        QThread::currentThread() == svc->thread()
            ? Qt::DirectConnection
            : Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(QString, id),
        Q_ARG(QString, provider),
        Q_ARG(QString, cwd),
        Q_ARG(QString, prompt),
        Q_ARG(QString, title));
    if (!ok) {
        PM_WARN("agent_spawn: invokeMethod(spawn) failed");
    }
    return id;
}

QString invokeLastError(QObject* svc) {
    QString err;
    QMetaObject::invokeMethod(
        svc, "lastError",
        QThread::currentThread() == svc->thread()
            ? Qt::DirectConnection
            : Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(QString, err));
    return err;
}

void invokeSend(QObject* svc, const QString& id, const QString& text) {
    QMetaObject::invokeMethod(
        svc, "send",
        QThread::currentThread() == svc->thread()
            ? Qt::DirectConnection
            : Qt::BlockingQueuedConnection,
        Q_ARG(QString, id),
        Q_ARG(QString, text));
}

void invokeStop(QObject* svc, const QString& id) {
    QMetaObject::invokeMethod(
        svc, "stop",
        QThread::currentThread() == svc->thread()
            ? Qt::DirectConnection
            : Qt::BlockingQueuedConnection,
        Q_ARG(QString, id));
}

QVariantMap invokeStatus(QObject* svc, const QString& id) {
    QVariantMap out;
    QMetaObject::invokeMethod(
        svc, "status",
        QThread::currentThread() == svc->thread()
            ? Qt::DirectConnection
            : Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(QVariantMap, out),
        Q_ARG(QString, id));
    return out;
}

nlohmann::json qvariantMapToJson(const QVariantMap& m);

nlohmann::json qvariantToJson(const QVariant& v) {
    if (v.typeId() == QMetaType::QString)
        return v.toString().toStdString();
    if (v.typeId() == QMetaType::Bool)
        return v.toBool();
    if (v.typeId() == QMetaType::Int || v.typeId() == QMetaType::LongLong
        || v.typeId() == QMetaType::UInt || v.typeId() == QMetaType::ULongLong)
        return v.toLongLong();
    if (v.typeId() == QMetaType::Double || v.typeId() == QMetaType::Float)
        return v.toDouble();
    if (v.canConvert<QVariantList>()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const QVariant& item : v.toList())
            arr.push_back(qvariantToJson(item));
        return arr;
    }
    if (v.canConvert<QVariantMap>())
        return qvariantMapToJson(v.toMap());
    if (v.isNull() || !v.isValid())
        return nullptr;
    return v.toString().toStdString();
}

nlohmann::json qvariantMapToJson(const QVariantMap& m) {
    nlohmann::json j = nlohmann::json::object();
    for (auto it = m.constBegin(); it != m.constEnd(); ++it)
        j[it.key().toStdString()] = qvariantToJson(it.value());
    return j;
}

ToolResult noService(const char* tool) {
    return {false,
            {{"error", "AgentSessionService not available"},
             {"hint", "Sessions service is not wired; call setAgentSessionService "
                      "after AppController constructs AgentSessionService."}},
            std::string(tool) + ": sessions service missing"};
}

} // namespace

QObject* resolveAgentSessionService() {
    return agentSessionService();
}

// --- agent_spawn ------------------------------------------------------------

std::string AgentSpawnTool::name() const { return "agent_spawn"; }
std::string AgentSpawnTool::description() const {
    return "Spawn an external AI CLI agent session (claude-code, codex, pty). "
           "cwd MUST be inside agents.allowed_dirs (Settings ▸ Agents) or the tool "
           "refuses and asks you to have the user allow the path. Returns session id.";
}
nlohmann::json AgentSpawnTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"provider", {{"type", "string"},
                          {"description", "claude-code | codex | pty (or a pty profile name)"}}},
            {"cwd",      {{"type", "string"},
                          {"description", "Working directory (must be under agents.allowed_dirs)"}}},
            {"prompt",   {{"type", "string"},
                          {"description", "Initial prompt for the agent"}}},
            {"title",    {{"type", "string"},
                          {"description", "Optional card title"}}},
        }},
        {"required", {"cwd", "prompt"}},
    };
}

ToolResult AgentSpawnTool::invoke(const nlohmann::json& args, ToolContext& /*ctx*/) {
    QObject* svc = resolveAgentSessionService();
    if (!svc) return noService("agent_spawn");

    const QString provider = QString::fromStdString(args.value("provider", "claude-code"));
    const QString cwd      = QString::fromStdString(args.value("cwd", ""));
    const QString prompt   = QString::fromStdString(args.value("prompt", ""));
    const QString title    = QString::fromStdString(args.value("title", ""));

    const QString id = invokeSpawn(svc, provider, cwd, prompt, title);
    if (id.isEmpty()) {
        const QString err = invokeLastError(svc);
        const std::string msg = err.isEmpty()
            ? "spawn failed (empty id)"
            : err.toStdString();
        EventBus::instance().publishNotice(
            {"warn", "agents",
             QStringLiteral("agent_spawn refused: %1").arg(QString::fromStdString(msg))});
        return {false,
                {{"error", msg}, {"refused", true}},
                "agent_spawn: " + msg};
    }

    EventBus::instance().publishNotice(
        {"info", "agents",
         QStringLiteral("Spawned agent session %1 (%2)").arg(id, provider)});
    return {true,
            {{"session_id", id.toStdString()},
             {"provider", provider.toStdString()},
             {"cwd", cwd.toStdString()},
             {"title", title.toStdString()}},
            "Spawned session " + id.toStdString()};
}

// --- agent_send -------------------------------------------------------------

std::string AgentSendTool::name() const { return "agent_send"; }
std::string AgentSendTool::description() const {
    return "Send text to an existing external agent session (reply / continue).";
}
nlohmann::json AgentSendTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"id",   {{"type", "string"}, {"description", "Session id"}}},
            {"text", {{"type", "string"}, {"description", "Message to send"}}},
        }},
        {"required", {"id", "text"}},
    };
}

ToolResult AgentSendTool::invoke(const nlohmann::json& args, ToolContext& /*ctx*/) {
    QObject* svc = resolveAgentSessionService();
    if (!svc) return noService("agent_send");

    const QString id   = QString::fromStdString(args.value("id", ""));
    const QString text = QString::fromStdString(args.value("text", ""));
    if (id.isEmpty() || text.isEmpty())
        return {false, {{"error", "id and text required"}}, "agent_send: missing args"};

    invokeSend(svc, id, text);
    const QString err = invokeLastError(svc);
    if (!err.isEmpty())
        return {false, {{"error", err.toStdString()}, {"session_id", id.toStdString()}},
                "agent_send: " + err.toStdString()};

    return {true, {{"session_id", id.toStdString()}, {"sent", true}},
            "Sent to session " + id.toStdString()};
}

// --- agent_status -----------------------------------------------------------

std::string AgentStatusTool::name() const { return "agent_status"; }
std::string AgentStatusTool::description() const {
    return "Summarize one external agent session (by id) or all open sessions.";
}
nlohmann::json AgentStatusTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"id", {{"type", "string"},
                    {"description", "Optional session id; omit for all sessions"}}},
        }},
    };
}

ToolResult AgentStatusTool::invoke(const nlohmann::json& args, ToolContext& /*ctx*/) {
    QObject* svc = resolveAgentSessionService();
    if (!svc) return noService("agent_status");

    const QString id = QString::fromStdString(args.value("id", ""));
    const QVariantMap st = invokeStatus(svc, id);
    nlohmann::json content = qvariantMapToJson(st);
    content["session_id"] = id.toStdString();
    return {true, std::move(content),
            id.isEmpty() ? "Listed agent sessions" : ("Status for " + id.toStdString())};
}

// --- agent_stop -------------------------------------------------------------

std::string AgentStopTool::name() const { return "agent_stop"; }
std::string AgentStopTool::description() const {
    return "Stop an external agent session by id.";
}
nlohmann::json AgentStopTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"id", {{"type", "string"}, {"description", "Session id to stop"}}},
        }},
        {"required", {"id"}},
    };
}

ToolResult AgentStopTool::invoke(const nlohmann::json& args, ToolContext& /*ctx*/) {
    QObject* svc = resolveAgentSessionService();
    if (!svc) return noService("agent_stop");

    const QString id = QString::fromStdString(args.value("id", ""));
    if (id.isEmpty())
        return {false, {{"error", "id required"}}, "agent_stop: missing id"};

    invokeStop(svc, id);
    return {true, {{"session_id", id.toStdString()}, {"stopped", true}},
            "Stopped session " + id.toStdString()};
}

// --- agent_watch ------------------------------------------------------------

std::string AgentWatchTool::name() const { return "agent_watch"; }
std::string AgentWatchTool::description() const {
    return "Subscribe the current goal (or ambient) to external agent session events. "
           "notify=voice speaks needs-input; notify=toast shows a notice. Used by "
           "skills such as slop_mode.";
}
nlohmann::json AgentWatchTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"notify", {{"type", "string"},
                        {"enum", nlohmann::json::array({"voice", "toast"})},
                        {"description", "How to alert when a session needs input (default toast)"}}},
            {"goal_id", {{"type", "integer"},
                         {"description", "Optional goal id to attach the watch to"}}},
        }},
    };
}

ToolResult AgentWatchTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    const std::string notify = args.value("notify", "toast");
    if (notify != "voice" && notify != "toast")
        return {false, {{"error", "notify must be voice|toast"}}, "agent_watch: bad notify"};

    int64_t goal_id = args.value("goal_id", int64_t{0});
    nlohmann::json watch = {
        {"notify", notify},
        {"active", true},
        {"ts", tool_support::nowUnix()},
    };

    if (ctx.db && goal_id > 0) {
        std::string ctx_json = "{}";
        ctx.db->query("SELECT context_json FROM goals WHERE id=?1", {goal_id},
                      [&](const Row& r) { ctx_json = r.text(0); });
        auto cj = nlohmann::json::parse(ctx_json, nullptr, false);
        if (!cj.is_object()) cj = nlohmann::json::object();
        cj["agent_watch"] = watch;
        ctx.db->exec("UPDATE goals SET context_json=?2, updated_at=?3 WHERE id=?1",
                     {goal_id, cj.dump(), tool_support::nowUnix()});
        watch["goal_id"] = goal_id;
    }

    if (notify == "voice") {
        EventBus::instance().publishNotice(
            {"info", "agents",
             QStringLiteral("Watching agent sessions (voice on needs-input)")});
    } else {
        EventBus::instance().publishNotice(
            {"info", "agents",
             QStringLiteral("Watching agent sessions (toast on needs-input)")});
    }

    return {true, {{"watch", watch}},
            "agent_watch active (notify=" + notify + ")"};
}

} // namespace polymath
