#pragma once
//
// AgentSessionService — registry of IAgentProviders, session table, EventBus
// republish, allowlist / concurrency gates. Runs on its own QThread.
// Spec: docs/overhaul/05_AGENT_SESSIONS.md §1, §4–5.
//
#include "service.h"
#include "i_agent_provider.h"
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <vector>

namespace polymath {

class Database;
class Config;

class AgentSessionService : public QObject, public IService {
    Q_OBJECT
public:
    AgentSessionService(Database& db, Config& config, QObject* parent = nullptr);
    ~AgentSessionService() override;

    void start() override;
    void stop() override;
    const char* serviceName() const override { return "sessions"; }

    // --- Provider registry -------------------------------------------------
    void registerProvider(std::unique_ptr<IAgentProvider> provider);
    QStringList providerNames() const;
    QVariantList providerInfo() const;   // [{name, available, experimental}]
    IAgentProvider* provider(const QString& name) const;
    bool providerAvailable(const QString& name) const;

    // --- Allowlist / concurrency (also used by tests) ----------------------
    // Returns empty string if cwd is allowed; otherwise a human refusal reason.
    QString validateCwd(const QString& cwd) const;
    QStringList allowedDirs() const;
    int maxConcurrent() const;
    int activeCount() const;   // working | needs_input

    // --- Session lifecycle (call on service thread) ------------------------
    // Returns session id or empty + lastError() set on failure.
    Q_INVOKABLE QString spawn(const QString& provider, const QString& cwd,
                              const QString& prompt, const QString& title = {});
    Q_INVOKABLE void send(const QString& id, const QString& text);
    Q_INVOKABLE void stop(const QString& id);
    Q_INVOKABLE QVariantMap status(const QString& id = {}) const;  // one or all
    Q_INVOKABLE QString lastError() const { return last_error_; }

    // Persist / load helpers (public for SessionsModel refresh + tests).
    void ensureSchema();
    QVariantList listSessions(int limit = 200) const;

signals:
    // Local mirror of bus events for same-thread consumers / tests.
    void sessionEvent(const polymath::AgentEvent& e);

public slots:
    void onProviderEvent(const polymath::AgentEvent& e);

private:
    struct SessionRow {
        QString id;
        QString provider;
        QString title;
        QString cwd;
        QString status;   // working|needs_input|done|error|stopped
        QString native_session_id;
        double  cost_usd = 0;
        qint64  created_at = 0;
        qint64  updated_at = 0;
        QString last_message;
    };

    void upsertSession(const SessionRow& row);
    void updateFromEvent(const AgentEvent& e);
    QString statusForKind(AgentEvent::Kind k, const QString& text) const;
    void publishBus(const AgentEvent& e);
    void maybeSpeakNeedsInput(const SessionRow& row, const QString& text);
    void activity(const QString& action, const QString& detail, bool ok = true);

    Database& db_;
    Config&   config_;
    std::vector<std::unique_ptr<IAgentProvider>> providers_;
    QHash<QString, SessionRow> live_;   // in-memory cache of open sessions
    QString last_error_;
    bool running_ = false;
};

} // namespace polymath
