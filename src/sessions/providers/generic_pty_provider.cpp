#include "generic_pty_provider.h"
#include "logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

namespace polymath {

GenericPtyProvider::GenericPtyProvider(QObject* parent) : IAgentProvider(parent) {
    // Built-in minimal "echo" profile for tests / demos when no JSON present.
    PtyProfile echo;
    echo.name = QStringLiteral("echo");
    echo.command = QStringLiteral("cmd");
#ifdef Q_OS_WIN
    echo.args_template = {QStringLiteral("/c"), QStringLiteral("echo"), QStringLiteral("{prompt}")};
#else
    echo.command = QStringLiteral("sh");
    echo.args_template = {QStringLiteral("-c"), QStringLiteral("echo {prompt}")};
#endif
    echo.done_patterns = {QRegularExpression(QStringLiteral("."))};
    echo.idle_timeout_s = 5;
    profiles_.push_back(echo);
}

GenericPtyProvider::~GenericPtyProvider() {
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
        if (it->idle) {
            it->idle->stop();
            it->idle->deleteLater();
        }
        if (it->proc) {
            it->proc->kill();
            it->proc->waitForFinished(1500);
        }
    }
}

void GenericPtyProvider::setProfiles(QVector<PtyProfile> profiles) {
    if (!profiles.isEmpty())
        profiles_ = std::move(profiles);
}

void GenericPtyProvider::loadProfiles(const QString& dir) {
    QDir d(dir);
    if (!d.exists())
        return;
    QVector<PtyProfile> loaded;
    for (const QString& name : d.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile f(d.filePath(name));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject())
            continue;
        const QJsonObject o = doc.object();
        PtyProfile p;
        p.name = o.value(QStringLiteral("name")).toString(QFileInfo(name).baseName());
        p.command = o.value(QStringLiteral("command")).toString();
        for (const QJsonValue& v : o.value(QStringLiteral("args_template")).toArray())
            p.args_template << v.toString();
        auto loadRe = [](const QJsonArray& arr) {
            QVector<QRegularExpression> out;
            for (const QJsonValue& v : arr)
                out.push_back(QRegularExpression(v.toString(),
                    QRegularExpression::CaseInsensitiveOption));
            return out;
        };
        p.needs_input_patterns = loadRe(o.value(QStringLiteral("needs_input_patterns")).toArray());
        p.done_patterns = loadRe(o.value(QStringLiteral("done_patterns")).toArray());
        p.error_patterns = loadRe(o.value(QStringLiteral("error_patterns")).toArray());
        p.idle_timeout_s = o.value(QStringLiteral("idle_timeout_s")).toInt(30);
        if (!p.command.isEmpty())
            loaded.push_back(p);
    }
    if (!loaded.isEmpty())
        profiles_ = std::move(loaded);
}

bool GenericPtyProvider::available() const {
    // Always "available" as a catch-all — individual profiles may still fail to spawn.
    return !profiles_.isEmpty();
}

const PtyProfile* GenericPtyProvider::findProfile(const QString& name) const {
    if (name.isEmpty() || name == QLatin1String("pty"))
        return profiles_.isEmpty() ? nullptr : &profiles_.first();
    for (const auto& p : profiles_)
        if (p.name == name)
            return &p;
    return profiles_.isEmpty() ? nullptr : &profiles_.first();
}

QStringList GenericPtyProvider::expandArgs(const QStringList& tmpl, const SpawnSpec& spec) const {
    QStringList out;
    for (QString a : tmpl) {
        a.replace(QStringLiteral("{prompt}"), spec.prompt);
        a.replace(QStringLiteral("{text}"), spec.prompt);
        a.replace(QStringLiteral("{cwd}"), spec.cwd);
        out << a;
    }
    return out;
}

AgentEvent::Kind GenericPtyProvider::classifyChunk(const QString& text,
                                                   const PtyProfile& profile) {
    for (const auto& re : profile.error_patterns)
        if (re.isValid() && re.match(text).hasMatch())
            return AgentEvent::Error;
    for (const auto& re : profile.needs_input_patterns)
        if (re.isValid() && re.match(text).hasMatch())
            return AgentEvent::NeedsInput;
    for (const auto& re : profile.done_patterns)
        if (re.isValid() && re.match(text).hasMatch())
            return AgentEvent::Result;
    return AgentEvent::AssistantText;
}

QString GenericPtyProvider::spawn(const SpawnSpec& spec) {
    // provider field may be "pty" or a profile name; extra_args[0] can override profile.
    QString profileName = spec.provider;
    if (!spec.extra_args.isEmpty() && !spec.extra_args.first().startsWith(QLatin1Char('-')))
        profileName = spec.extra_args.first();
    const PtyProfile* profile = findProfile(profileName);
    if (!profile) {
        PM_WARN("GenericPtyProvider: no profiles loaded");
        return {};
    }

    QString id = pending_id_;
    pending_id_.clear();
    if (id.isEmpty())
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    Slot slot;
    slot.id = id;
    slot.cwd = spec.cwd;
    slot.profile_name = profile->name;
    slots_.insert(id, slot);

    QStringList args = expandArgs(profile->args_template, spec);
    // Append remaining extra_args that look like flags.
    for (const QString& a : spec.extra_args) {
        if (a.startsWith(QLatin1Char('-')))
            args << a;
    }
    startProcess(slots_[id], *profile, args);

    AgentEvent started;
    started.session_id = id;
    started.kind = AgentEvent::Started;
    started.text = QStringLiteral("spawned pty/%1").arg(profile->name);
    started.ts = QDateTime::currentMSecsSinceEpoch();
    emit event(started);
    return id;
}

void GenericPtyProvider::send(const QString& id, const QString& text) {
    auto it = slots_.find(id);
    if (it == slots_.end() || !it->proc || it->proc->state() == QProcess::NotRunning)
        return;
    // Best-effort stdin write for interactive CLIs.
    it->proc->write(text.toUtf8());
    it->proc->write("\n");
}

void GenericPtyProvider::stop(const QString& id) {
    auto it = slots_.find(id);
    if (it == slots_.end())
        return;
    if (it->idle) {
        it->idle->stop();
        it->idle->deleteLater();
        it->idle = nullptr;
    }
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

void GenericPtyProvider::startProcess(Slot& slot, const PtyProfile& profile,
                                      const QStringList& args) {
    if (slot.proc) {
        slot.proc->kill();
        slot.proc->deleteLater();
        slot.proc = nullptr;
    }
    auto* proc = new QProcess(this);
    slot.proc = proc;
    slot.buf.clear();
    slot.tail.clear();
    proc->setProgram(profile.command);
    proc->setArguments(args);
    if (!slot.cwd.isEmpty())
        proc->setWorkingDirectory(slot.cwd);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    const QString id = slot.id;
    connect(proc, &QProcess::readyRead, this, [this, id]() { onReadyRead(id); });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, id](int code, QProcess::ExitStatus) { onFinished(id, code); });

    // Idle timer: no output for N s while alive → NeedsInput heuristic.
    if (!slot.idle) {
        slot.idle = new QTimer(this);
        slot.idle->setSingleShot(true);
        connect(slot.idle, &QTimer::timeout, this, [this, id]() { onIdle(id); });
    }
    slot.idle->setInterval(qMax(3, profile.idle_timeout_s) * 1000);

    proc->start();
    if (!proc->waitForStarted(5000)) {
        AgentEvent e;
        e.session_id = id;
        e.kind = AgentEvent::Error;
        e.text = QStringLiteral("failed to start %1: %2")
                     .arg(profile.command, proc->errorString());
        e.ts = QDateTime::currentMSecsSinceEpoch();
        emit event(e);
        proc->deleteLater();
        slot.proc = nullptr;
        return;
    }
    slot.idle->start();
}

void GenericPtyProvider::onReadyRead(const QString& id) {
    auto it = slots_.find(id);
    if (it == slots_.end() || !it->proc)
        return;
    const QByteArray chunk = it->proc->readAll();
    if (chunk.isEmpty())
        return;
    it->buf.append(chunk);
    it->tail = QString::fromLocal8Bit(it->buf.right(4000));
    if (it->idle)
        it->idle->start();  // reset idle

    const PtyProfile* profile = findProfile(it->profile_name);
    if (!profile)
        return;
    const QString text = QString::fromLocal8Bit(chunk).trimmed();
    if (text.isEmpty())
        return;
    AgentEvent e;
    e.session_id = id;
    e.kind = classifyChunk(it->tail, *profile);
    e.text = text.left(500);
    e.raw_json = text.left(2000);
    e.ts = QDateTime::currentMSecsSinceEpoch();
    emit event(e);
}

void GenericPtyProvider::onFinished(const QString& id, int exitCode) {
    auto it = slots_.find(id);
    if (it == slots_.end())
        return;
    if (it->idle) {
        it->idle->stop();
    }
    if (it->proc) {
        const QByteArray rest = it->proc->readAll();
        if (!rest.isEmpty()) {
            it->tail += QString::fromLocal8Bit(rest);
        }
        it->proc->deleteLater();
        it->proc = nullptr;
    }
    AgentEvent e;
    e.session_id = id;
    e.kind = (exitCode == 0) ? AgentEvent::Result : AgentEvent::Error;
    e.text = it->tail.right(400);
    if (e.text.isEmpty())
        e.text = (exitCode == 0) ? QStringLiteral("done")
                                 : QStringLiteral("exit %1").arg(exitCode);
    e.raw_json = it->tail.right(2000);
    e.ts = QDateTime::currentMSecsSinceEpoch();
    emit event(e);
}

void GenericPtyProvider::onIdle(const QString& id) {
    auto it = slots_.find(id);
    if (it == slots_.end() || !it->proc || it->proc->state() == QProcess::NotRunning)
        return;
    AgentEvent e;
    e.session_id = id;
    e.kind = AgentEvent::NeedsInput;
    e.text = QStringLiteral("(session appears to be waiting)");
    e.raw_json = it->tail.right(2000);
    e.ts = QDateTime::currentMSecsSinceEpoch();
    emit event(e);
}

} // namespace polymath

#include <QFileInfo>
