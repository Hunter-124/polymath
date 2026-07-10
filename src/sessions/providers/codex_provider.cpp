#include "codex_provider.h"
#include "claude_stream_parse.h"
#include "logging.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

namespace polymath {

CodexProvider::CodexProvider(QObject* parent) : IAgentProvider(parent) {}

CodexProvider::~CodexProvider() {
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
        if (it->proc) {
            it->proc->kill();
            it->proc->waitForFinished(1500);
        }
    }
}

QString CodexProvider::resolveBinary() {
    const QByteArray env = qgetenv("CODEX_BIN");
    if (!env.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(env)))
        return QString::fromLocal8Bit(env);
    QString found = QStandardPaths::findExecutable(QStringLiteral("codex"));
#ifdef Q_OS_WIN
    if (found.isEmpty())
        found = QStandardPaths::findExecutable(QStringLiteral("codex.cmd"));
#endif
    return found;
}

bool CodexProvider::available() const { return !resolveBinary().isEmpty(); }

QVector<AgentEvent> CodexProvider::parseLine(const QString& line, const QString& session_id) {
    QVector<AgentEvent> out;
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return out;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // Fallback: treat non-JSON as raw assistant text (lossy).
        if (!trimmed.isEmpty()) {
            AgentEvent e;
            e.session_id = session_id;
            e.kind = AgentEvent::AssistantText;
            e.text = trimmed;
            e.raw_json = trimmed;
            e.ts = QDateTime::currentMSecsSinceEpoch();
            out.push_back(e);
        }
        return out;
    }
    const QJsonObject o = doc.object();
    const QString type = o.value(QStringLiteral("type")).toString(
        o.value(QStringLiteral("event")).toString());
    AgentEvent e;
    e.session_id = session_id;
    e.raw_json = trimmed;
    e.ts = QDateTime::currentMSecsSinceEpoch();
    e.text = o.value(QStringLiteral("text")).toString(
        o.value(QStringLiteral("message")).toString(
            o.value(QStringLiteral("content")).toString()));

    if (type.contains(QLatin1String("error"), Qt::CaseInsensitive)) {
        e.kind = AgentEvent::Error;
        if (e.text.isEmpty()) e.text = QStringLiteral("codex error");
    } else if (type.contains(QLatin1String("result"), Qt::CaseInsensitive)
            || type.contains(QLatin1String("completed"), Qt::CaseInsensitive)
            || type == QLatin1String("agent_message")) {
        e.kind = AgentEvent::Result;
        if (e.text.isEmpty())
            e.text = o.value(QStringLiteral("result")).toString(QStringLiteral("done"));
        e.cost_usd = o.value(QStringLiteral("total_cost_usd")).toDouble(
            o.value(QStringLiteral("cost_usd")).toDouble(0));
    } else if (type.contains(QLatin1String("permission"), Qt::CaseInsensitive)
            || type.contains(QLatin1String("input"), Qt::CaseInsensitive)
            || looksLikeNeedsInput(e.text, trimmed)) {
        e.kind = AgentEvent::NeedsInput;
        if (e.text.isEmpty()) e.text = QStringLiteral("codex needs input");
    } else if (type.contains(QLatin1String("tool"), Qt::CaseInsensitive)) {
        e.kind = AgentEvent::ToolUse;
        if (e.text.isEmpty()) e.text = type;
    } else if (type.contains(QLatin1String("start"), Qt::CaseInsensitive)
            || type.contains(QLatin1String("session"), Qt::CaseInsensitive)) {
        e.kind = AgentEvent::Started;
        e.native_session_id = o.value(QStringLiteral("session_id")).toString(
            o.value(QStringLiteral("thread_id")).toString());
        if (e.text.isEmpty()) e.text = QStringLiteral("codex started");
    } else {
        e.kind = AgentEvent::AssistantText;
        if (e.text.isEmpty()) e.text = type.isEmpty() ? trimmed : type;
    }
    out.push_back(e);
    return out;
}

QString CodexProvider::spawn(const SpawnSpec& spec) {
    const QString bin = resolveBinary();
    if (bin.isEmpty())
        return {};
    QString id = pending_id_;
    pending_id_.clear();
    if (id.isEmpty())
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    Slot slot;
    slot.id = id;
    slot.cwd = spec.cwd;
    slots_.insert(id, slot);

    QStringList args;
    if (spec.resume && !spec.resume_id.isEmpty()) {
        // Best-effort resume flag; exact CLI may vary by codex version.
        args << QStringLiteral("exec") << QStringLiteral("resume") << spec.resume_id
             << QStringLiteral("--json") << spec.prompt;
    } else {
        args << QStringLiteral("exec") << QStringLiteral("--json") << spec.prompt;
    }
    args += spec.extra_args;
    startProcess(slots_[id], args);

    AgentEvent started;
    started.session_id = id;
    started.kind = AgentEvent::Started;
    started.text = QStringLiteral("spawned codex");
    started.ts = QDateTime::currentMSecsSinceEpoch();
    emit event(started);
    return id;
}

void CodexProvider::send(const QString& id, const QString& text) {
    auto it = slots_.find(id);
    if (it == slots_.end())
        return;
    if (it->proc && it->proc->state() != QProcess::NotRunning)
        return;
    QStringList args;
    if (!it->native_id.isEmpty())
        args << QStringLiteral("exec") << QStringLiteral("resume") << it->native_id
             << QStringLiteral("--json") << text;
    else
        args << QStringLiteral("exec") << QStringLiteral("--json") << text;
    startProcess(*it, args);
}

void CodexProvider::stop(const QString& id) {
    auto it = slots_.find(id);
    if (it == slots_.end())
        return;
    if (it->proc) {
        it->proc->kill();
        it->proc->waitForFinished(2000);
        it->proc->deleteLater();
        it->proc = nullptr;
    }
    AgentEvent e;
    e.session_id = id;
    e.kind = AgentEvent::Result;
    e.text = QStringLiteral("stopped");
    e.raw_json = QStringLiteral("{\"polymath_stopped\":true}");
    e.ts = QDateTime::currentMSecsSinceEpoch();
    emit event(e);
}

void CodexProvider::startProcess(Slot& slot, const QStringList& args) {
    const QString bin = resolveBinary();
    if (slot.proc) {
        slot.proc->kill();
        slot.proc->deleteLater();
        slot.proc = nullptr;
    }
    auto* proc = new QProcess(this);
    slot.proc = proc;
    slot.buf.clear();
    proc->setProgram(bin);
    proc->setArguments(args);
    if (!slot.cwd.isEmpty())
        proc->setWorkingDirectory(slot.cwd);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    const QString id = slot.id;
    connect(proc, &QProcess::readyRead, this, [this, id]() { onReadyRead(id); });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, id](int code, QProcess::ExitStatus) { onFinished(id, code); });
    proc->start();
    if (!proc->waitForStarted(5000)) {
        AgentEvent e;
        e.session_id = id;
        e.kind = AgentEvent::Error;
        e.text = QStringLiteral("failed to start codex: %1").arg(proc->errorString());
        e.ts = QDateTime::currentMSecsSinceEpoch();
        emit event(e);
        proc->deleteLater();
        slot.proc = nullptr;
    }
}

void CodexProvider::onReadyRead(const QString& id) {
    auto it = slots_.find(id);
    if (it == slots_.end() || !it->proc)
        return;
    it->buf.append(it->proc->readAll());
    int nl;
    while ((nl = it->buf.indexOf('\n')) >= 0) {
        const QByteArray line = it->buf.left(nl);
        it->buf.remove(0, nl + 1);
        for (const auto& e : parseLine(QString::fromUtf8(line), id)) {
            if (!e.native_session_id.isEmpty())
                it->native_id = e.native_session_id;
            emit event(e);
        }
    }
}

void CodexProvider::onFinished(const QString& id, int exitCode) {
    auto it = slots_.find(id);
    if (it == slots_.end())
        return;
    if (it->proc) {
        it->buf.append(it->proc->readAll());
        if (!it->buf.isEmpty()) {
            for (const auto& e : parseLine(QString::fromUtf8(it->buf), id))
                emit event(e);
            it->buf.clear();
        }
        it->proc->deleteLater();
        it->proc = nullptr;
    }
    if (exitCode != 0) {
        AgentEvent e;
        e.session_id = id;
        e.kind = AgentEvent::Error;
        e.text = QStringLiteral("codex exited with code %1").arg(exitCode);
        e.ts = QDateTime::currentMSecsSinceEpoch();
        emit event(e);
    }
}

} // namespace polymath
