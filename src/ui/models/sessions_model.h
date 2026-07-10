#pragma once
//
// SessionsModel — live cards for external agent sessions.
// Context property "agentSessions". Spec: docs/overhaul/05_AGENT_SESSIONS.md §3.
//
#include "event_bus.h"
#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace polymath {

class AgentSessionService;
class Database;

class SessionsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        ProviderRole,
        TitleRole,
        CwdRole,
        StatusRole,
        LastMessageRole,
        CostUsdRole,
        ElapsedRole,
        UnreadPingRole,
        ExperimentalRole,
        CreatedAtRole,
        UpdatedAtRole
    };

    explicit SessionsModel(Database& db, QObject* parent = nullptr);

    // Non-owning; may be null in capture/tests. Invokables no-op without it.
    void setService(AgentSessionService* svc);
    AgentSessionService* service() const { return service_; }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString spawn(const QString& provider, const QString& cwd,
                              const QString& prompt, const QString& title = {});
    Q_INVOKABLE void send(const QString& id, const QString& text);
    Q_INVOKABLE void stop(const QString& id);
    Q_INVOKABLE void clearPing(const QString& id);
    Q_INVOKABLE QVariantList availableProviders() const;
    Q_INVOKABLE QStringList eventLog(const QString& id) const;
    Q_INVOKABLE QString lastError() const { return last_error_; }

public slots:
    void onAgentSessionEvent(const polymath::AgentSessionEvent& e);

signals:
    void countChanged();
    void spawnFailed(const QString& reason);

private:
    struct Session {
        QString id;
        QString provider;
        QString title;
        QString cwd;
        QString status;        // working|needs_input|done|error|stopped
        QString lastMessage;
        double  costUsd = 0;
        qint64  createdAt = 0;
        qint64  updatedAt = 0;
        bool    unreadPing = false;
        bool    experimental = false;
        QStringList events;    // recent raw_json / text lines (cap 100)
    };

    int  rowForId(const QString& id) const;
    void applyEvent(Session& s, const AgentSessionEvent& e);
    static QString formatElapsed(qint64 createdAtSec);

    Database& db_;
    AgentSessionService* service_ = nullptr;
    QVector<Session> rows_;
    QString last_error_;
};

} // namespace polymath
