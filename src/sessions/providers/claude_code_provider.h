#pragma once
//
// ClaudeCodeProvider — headless `claude -p --output-format stream-json` sessions.
// Spec: docs/overhaul/05_AGENT_SESSIONS.md §2.1.
// Permission policy: never auto-approve; NeedsInput is always relayed.
//
#include "i_agent_provider.h"
#include <QHash>
#include <QPointer>
#include <memory>

class QProcess;

namespace polymath {

class ClaudeCodeProvider : public IAgentProvider {
    Q_OBJECT
public:
    explicit ClaudeCodeProvider(QObject* parent = nullptr);
    ~ClaudeCodeProvider() override;

    QString name() const override { return QStringLiteral("claude-code"); }
    bool    available() const override;
    QString spawn(const SpawnSpec& spec) override;
    void    send(const QString& id, const QString& text) override;
    void    stop(const QString& id) override;
    void    setSessionId(const QString& id) override { pending_id_ = id; }

    // Resolve `claude` on PATH (or CLAUDE_BIN). Empty if missing.
    static QString resolveBinary();

private:
    struct Slot {
        QString id;
        QString cwd;
        QString native_id;
        QString title;
        QPointer<QProcess> proc;
        QByteArray stdout_buf;
    };

    void startProcess(Slot& slot, const QStringList& args);
    void onReadyRead(const QString& id);
    void onFinished(const QString& id, int exitCode, int exitStatus);
    void emitParsed(Slot& slot, const QByteArray& chunk);

    QHash<QString, Slot> slots_;
    QString pending_id_;
};

} // namespace polymath
