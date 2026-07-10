#pragma once
//
// GenericPtyProvider — catch-all CLI wrapper (grok, aider, …).
// Config-driven profiles under data/agent_providers/*.json.
// v1: QProcess with merged channels + regex state detection. Spec: 05 §2.3.
//
#include "i_agent_provider.h"
#include <QHash>
#include <QPointer>
#include <QRegularExpression>
#include <QVector>

class QProcess;
class QTimer;

namespace polymath {

struct PtyProfile {
    QString name;
    QString command;
    QStringList args_template;   // {prompt}, {cwd}, {text}
    QVector<QRegularExpression> needs_input_patterns;
    QVector<QRegularExpression> done_patterns;
    QVector<QRegularExpression> error_patterns;
    int idle_timeout_s = 30;
};

class GenericPtyProvider : public IAgentProvider {
    Q_OBJECT
public:
    explicit GenericPtyProvider(QObject* parent = nullptr);
    ~GenericPtyProvider() override;

    QString name() const override { return QStringLiteral("pty"); }
    bool    available() const override;
    QString spawn(const SpawnSpec& spec) override;
    void    send(const QString& id, const QString& text) override;
    void    stop(const QString& id) override;
    void    setSessionId(const QString& id) override { pending_id_ = id; }

    // Load / replace profiles from a directory of *.json (or use built-in defaults).
    void loadProfiles(const QString& dir);
    void setProfiles(QVector<PtyProfile> profiles);
    const QVector<PtyProfile>& profiles() const { return profiles_; }

    // Apply regex heuristics to a text chunk (fixture-testable).
    static AgentEvent::Kind classifyChunk(const QString& text, const PtyProfile& profile);

private:
    struct Slot {
        QString id;
        QString cwd;
        QString profile_name;
        QPointer<QProcess> proc;
        QByteArray buf;
        QString tail;
        QTimer* idle = nullptr;
    };

    const PtyProfile* findProfile(const QString& name) const;
    QStringList expandArgs(const QStringList& tmpl, const SpawnSpec& spec) const;
    void startProcess(Slot& slot, const PtyProfile& profile, const QStringList& args);
    void onReadyRead(const QString& id);
    void onFinished(const QString& id, int exitCode);
    void onIdle(const QString& id);

    QVector<PtyProfile> profiles_;
    QHash<QString, Slot> slots_;
    QString pending_id_;
};

} // namespace polymath
