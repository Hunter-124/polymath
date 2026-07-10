#include "claude_code_provider.h"
#include "claude_stream_parse.h"
#include "logging.h"

#include <QDateTime>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUuid>

namespace polymath {

ClaudeCodeProvider::ClaudeCodeProvider(QObject* parent) : IAgentProvider(parent) {}

ClaudeCodeProvider::~ClaudeCodeProvider() {
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
        if (it->proc) {
            it->proc->kill();
            it->proc->waitForFinished(1500);
        }
    }
}

QString ClaudeCodeProvider::resolveBinary() {
    const QByteArray env = qgetenv("CLAUDE_BIN");
    if (!env.isEmpty()) {
        const QString p = QString::fromLocal8Bit(env);
        if (QFileInfo::exists(p))
            return p;
    }
    // Windows: claude.cmd / claude.exe from npm global; Unix: claude
    const QString found = QStandardPaths::findExecutable(QStringLiteral("claude"));
    if (!found.isEmpty())
        return found;
#ifdef Q_OS_WIN
    const QString cmd = QStandardPaths::findExecutable(QStringLiteral("claude.cmd"));
    if (!cmd.isEmpty())
        return cmd;
#endif
    return {};
}

bool ClaudeCodeProvider::available() const {
    return !resolveBinary().isEmpty();
}

QString ClaudeCodeProvider::spawn(const SpawnSpec& spec) {
    const QString bin = resolveBinary();
    if (bin.isEmpty()) {
        PM_WARN("ClaudeCodeProvider: claude not on PATH");
        return {};
    }

    QString id = pending_id_;
    pending_id_.clear();
    if (id.isEmpty())
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    Slot slot;
    slot.id = id;
    slot.cwd = spec.cwd;
    slot.title = spec.title;
    if (spec.resume && !spec.resume_id.isEmpty())
        slot.native_id = spec.resume_id;

    QStringList args;
    if (spec.resume && !spec.resume_id.isEmpty()) {
        args << QStringLiteral("--resume") << spec.resume_id;
    }
    args << QStringLiteral("-p") << spec.prompt
         << QStringLiteral("--output-format") << QStringLiteral("stream-json")
         << QStringLiteral("--verbose")
         << QStringLiteral("--include-partial-messages=false");
    args += spec.extra_args;

    slots_.insert(id, slot);
    startProcess(slots_[id], args);

    AgentEvent started;
    started.session_id = id;
    started.kind = AgentEvent::Started;
    started.text = QStringLiteral("spawned claude-code");
    started.ts = QDateTime::currentMSecsSinceEpoch();
    emit event(started);
    return id;
}

void ClaudeCodeProvider::send(const QString& id, const QString& text) {
    auto it = slots_.find(id);
    if (it == slots_.end()) {
        PM_WARN("ClaudeCodeProvider::send unknown session {}", id.toStdString());
        return;
    }
    // Headless -p sessions are turn-based: each send is a --resume continuation.
    if (it->proc && it->proc->state() != QProcess::NotRunning) {
        PM_WARN("ClaudeCodeProvider::send: process still running for {}", id.toStdString());
        return;
    }
    const QString native = it->native_id;
    SpawnSpec spec;
    spec.cwd = it->cwd;
    spec.prompt = text;
    spec.title = it->title;
    spec.resume = !native.isEmpty();
    spec.resume_id = native;
    pending_id_ = id;
    // Keep slot; re-spawn over it.
    QStringList args;
    if (!native.isEmpty())
        args << QStringLiteral("--resume") << native;
    args << QStringLiteral("-p") << text
         << QStringLiteral("--output-format") << QStringLiteral("stream-json")
         << QStringLiteral("--verbose")
         << QStringLiteral("--include-partial-messages=false");
    startProcess(*it, args);
}

void ClaudeCodeProvider::stop(const QString& id) {
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
    e.ts = QDateTime::currentMSecsSinceEpoch();
    // Service maps Result+"stopped" or we emit a custom note — status becomes stopped upstream.
    e.raw_json = QStringLiteral("{\"type\":\"result\",\"result\":\"stopped\",\"polymath_stopped\":true}");
    emit event(e);
}

void ClaudeCodeProvider::startProcess(Slot& slot, const QStringList& args) {
    const QString bin = resolveBinary();
    if (slot.proc) {
        slot.proc->kill();
        slot.proc->deleteLater();
        slot.proc = nullptr;
    }
    auto* proc = new QProcess(this);
    slot.proc = proc;
    slot.stdout_buf.clear();
    proc->setProgram(bin);
    proc->setArguments(args);
    if (!slot.cwd.isEmpty())
        proc->setWorkingDirectory(slot.cwd);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    const QString id = slot.id;
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, id]() {
        onReadyRead(id);
    });
    // Merged channels still may surface on readyRead — also hook error channel.
    connect(proc, &QProcess::readyRead, this, [this, id]() {
        onReadyRead(id);
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, id](int code, QProcess::ExitStatus st) {
                onFinished(id, code, static_cast<int>(st));
            });

    PM_INFO("ClaudeCodeProvider: starting {} {}", bin.toStdString(),
            args.join(QLatin1Char(' ')).toStdString());
    proc->start();
    if (!proc->waitForStarted(5000)) {
        AgentEvent e;
        e.session_id = id;
        e.kind = AgentEvent::Error;
        e.text = QStringLiteral("failed to start claude: %1").arg(proc->errorString());
        e.ts = QDateTime::currentMSecsSinceEpoch();
        emit event(e);
        proc->deleteLater();
        slot.proc = nullptr;
    }
}

void ClaudeCodeProvider::onReadyRead(const QString& id) {
    auto it = slots_.find(id);
    if (it == slots_.end() || !it->proc)
        return;
    const QByteArray chunk = it->proc->readAll();
    if (chunk.isEmpty())
        return;
    emitParsed(*it, chunk);
}

void ClaudeCodeProvider::emitParsed(Slot& slot, const QByteArray& chunk) {
    slot.stdout_buf.append(chunk);
    int nl;
    while ((nl = slot.stdout_buf.indexOf('\n')) >= 0) {
        const QByteArray line = slot.stdout_buf.left(nl);
        slot.stdout_buf.remove(0, nl + 1);
        QString native;
        const auto events = parseClaudeStreamLine(QString::fromUtf8(line), slot.id, &native);
        if (!native.isEmpty())
            slot.native_id = native;
        for (auto e : events) {
            if (!native.isEmpty())
                e.native_session_id = slot.native_id;
            emit event(e);
        }
    }
}

void ClaudeCodeProvider::onFinished(const QString& id, int exitCode, int /*exitStatus*/) {
    auto it = slots_.find(id);
    if (it == slots_.end())
        return;
    // Flush remainder
    if (it->proc) {
        const QByteArray rest = it->proc->readAll();
        if (!rest.isEmpty())
            emitParsed(*it, rest);
        if (!it->stdout_buf.isEmpty()) {
            const auto events = parseClaudeStreamLine(QString::fromUtf8(it->stdout_buf),
                                                      it->id, nullptr);
            for (const auto& e : events)
                emit event(e);
            it->stdout_buf.clear();
        }
        it->proc->deleteLater();
        it->proc = nullptr;
    }
    if (exitCode != 0) {
        // If the stream already emitted Result/Error, this is a safety net only when
        // nothing terminal arrived. Emit a soft Error so the card does not hang on working.
        AgentEvent e;
        e.session_id = id;
        e.kind = AgentEvent::Error;
        e.text = QStringLiteral("claude exited with code %1").arg(exitCode);
        e.ts = QDateTime::currentMSecsSinceEpoch();
        e.raw_json = QStringLiteral("{\"type\":\"error\",\"exit_code\":%1}").arg(exitCode);
        emit event(e);
    }
}

} // namespace polymath
