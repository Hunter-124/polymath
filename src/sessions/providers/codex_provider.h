#pragma once
//
// CodexProvider — experimental adapter over `codex exec --json`.
// Same IAgentProvider surface; UI shows experimental badge. Spec: 05 §2.2.
//
#include "i_agent_provider.h"
#include <QHash>
#include <QPointer>

class QProcess;

namespace polymath {

class CodexProvider : public IAgentProvider {
    Q_OBJECT
public:
    explicit CodexProvider(QObject* parent = nullptr);
    ~CodexProvider() override;

    QString name() const override { return QStringLiteral("codex"); }
    bool    available() const override;
    bool    experimental() const override { return true; }
    QString spawn(const SpawnSpec& spec) override;
    void    send(const QString& id, const QString& text) override;
    void    stop(const QString& id) override;
    void    setSessionId(const QString& id) override { pending_id_ = id; }

    static QString resolveBinary();
    // Parse one codex --json line into AgentEvents (fixture-testable).
    static QVector<AgentEvent> parseLine(const QString& line, const QString& session_id);

private:
    struct Slot {
        QString id;
        QString cwd;
        QString native_id;
        QPointer<QProcess> proc;
        QByteArray buf;
    };

    void startProcess(Slot& slot, const QStringList& args);
    void onReadyRead(const QString& id);
    void onFinished(const QString& id, int exitCode);

    QHash<QString, Slot> slots_;
    QString pending_id_;
};

} // namespace polymath
