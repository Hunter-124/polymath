#pragma once
//
// IAgentProvider — adapter over an external AI CLI (Claude Code, Codex, PTY…).
// Spec: docs/overhaul/05_AGENT_SESSIONS.md §1–2.
//
#include <QObject>
#include <QString>
#include <QStringList>

namespace polymath {

struct SpawnSpec {
    QString provider;
    QString cwd;
    QString prompt;
    QString title;
    QStringList extra_args;
    bool    resume = false;
    QString resume_id;
};

// Normalized cross-provider event (provider-local; service maps to EventBus::AgentSessionEvent).
struct AgentEvent {
    QString session_id;
    enum Kind {
        Started,
        Thinking,
        ToolUse,
        AssistantText,
        NeedsInput,
        PermissionRequest,
        Result,
        Error,
        CostUpdate
    } kind = Started;
    QString text;        // human-readable payload
    QString raw_json;    // provider-native line for drill-down
    double  cost_usd = 0;
    qint64  ts = 0;
    // Provider-private: native CLI session id (from system/init).
    QString native_session_id;
};

inline QString agentEventKindName(AgentEvent::Kind k) {
    switch (k) {
    case AgentEvent::Started:            return QStringLiteral("Started");
    case AgentEvent::Thinking:           return QStringLiteral("Thinking");
    case AgentEvent::ToolUse:            return QStringLiteral("ToolUse");
    case AgentEvent::AssistantText:      return QStringLiteral("AssistantText");
    case AgentEvent::NeedsInput:         return QStringLiteral("NeedsInput");
    case AgentEvent::PermissionRequest:  return QStringLiteral("PermissionRequest");
    case AgentEvent::Result:             return QStringLiteral("Result");
    case AgentEvent::Error:              return QStringLiteral("Error");
    case AgentEvent::CostUpdate:         return QStringLiteral("CostUpdate");
    }
    return QStringLiteral("Unknown");
}

class IAgentProvider : public QObject {
    Q_OBJECT
public:
    explicit IAgentProvider(QObject* parent = nullptr) : QObject(parent) {}
    ~IAgentProvider() override = default;

    virtual QString name() const = 0;           // "claude-code" | "codex" | "pty"
    virtual bool    available() const = 0;      // binary on PATH?
    virtual bool    experimental() const { return false; }

    // Returns Polymath session_id (caller-assigned or provider-generated). Empty on refuse.
    virtual QString spawn(const SpawnSpec& spec) = 0;
    virtual void    send(const QString& id, const QString& text) = 0;
    virtual void    stop(const QString& id) = 0;

    // Optional: set the Polymath session id the provider should stamp on events
    // before spawn() when the service pre-allocates the id.
    virtual void setSessionId(const QString& /*id*/) {}

signals:
    void event(const polymath::AgentEvent& e);
};

} // namespace polymath

Q_DECLARE_METATYPE(polymath::AgentEvent)
Q_DECLARE_METATYPE(polymath::SpawnSpec)
