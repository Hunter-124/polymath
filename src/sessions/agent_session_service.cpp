#include "agent_session_service.h"
#include "providers/claude_code_provider.h"
#include "providers/codex_provider.h"
#include "providers/generic_pty_provider.h"
#include "activity_log.h"
#include "config.h"
#include "database.h"
#include "event_bus.h"
#include "logging.h"
#include "paths.h"
#include "types.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUuid>

namespace polymath {

AgentSessionService::AgentSessionService(Database& db, Config& config, QObject* parent)
    : QObject(parent), db_(db), config_(config) {
    qRegisterMetaType<polymath::AgentEvent>("polymath::AgentEvent");
}

AgentSessionService::~AgentSessionService() { stop(); }

void AgentSessionService::ensureSchema() {
    // Self-contained table (C4 owns sessions; core schema left untouched).
    db_.exec(R"SQL(
CREATE TABLE IF NOT EXISTS agent_sessions (
    id                 TEXT PRIMARY KEY,
    provider           TEXT NOT NULL,
    title              TEXT DEFAULT '',
    cwd                TEXT DEFAULT '',
    status             TEXT DEFAULT 'working',
    native_session_id  TEXT DEFAULT '',
    cost_usd           REAL DEFAULT 0,
    created_at         INTEGER NOT NULL,
    updated_at         INTEGER NOT NULL,
    last_message       TEXT DEFAULT ''
);
)SQL");
    db_.exec("CREATE INDEX IF NOT EXISTS idx_agent_sessions_updated "
             "ON agent_sessions(updated_at DESC);");
}

void AgentSessionService::start() {
    if (running_) return;
    ensureSchema();

    // Default providers (if none registered by tests).
    if (providers_.empty()) {
        registerProvider(std::make_unique<ClaudeCodeProvider>());
        registerProvider(std::make_unique<CodexProvider>());
        auto pty = std::make_unique<GenericPtyProvider>();
        const auto dir = Paths::instance().root() / "agent_providers";
        pty->loadProfiles(QString::fromStdString(dir.string()));
        registerProvider(std::move(pty));
    }

    // Wire every provider into onProviderEvent.
    for (auto& p : providers_) {
        connect(p.get(), &IAgentProvider::event,
                this, &AgentSessionService::onProviderEvent,
                Qt::DirectConnection);
    }

    // Hydrate live_ from DB (open sessions only).
    db_.query(
        "SELECT id,provider,title,cwd,status,native_session_id,cost_usd,"
        "created_at,updated_at,last_message FROM agent_sessions "
        "WHERE status IN ('working','needs_input') ORDER BY updated_at DESC LIMIT 50",
        {},
        [this](const Row& r) {
            SessionRow s;
            s.id = QString::fromStdString(r.text(0));
            s.provider = QString::fromStdString(r.text(1));
            s.title = QString::fromStdString(r.text(2));
            s.cwd = QString::fromStdString(r.text(3));
            s.status = QString::fromStdString(r.text(4));
            s.native_session_id = QString::fromStdString(r.text(5));
            s.cost_usd = r.dbl(6);
            s.created_at = r.i64(7);
            s.updated_at = r.i64(8);
            s.last_message = QString::fromStdString(r.text(9));
            live_.insert(s.id, s);
        });

    running_ = true;
    PM_INFO("AgentSessionService started ({} providers, {} open sessions)",
            providers_.size(), live_.size());
}

void AgentSessionService::stop() {
    if (!running_ && providers_.empty())
        return;
    for (auto& p : providers_) {
        // Stop every live session for this provider.
        for (auto it = live_.begin(); it != live_.end(); ++it) {
            if (it->provider == p->name())
                p->stop(it->id);
        }
    }
    running_ = false;
}

void AgentSessionService::registerProvider(std::unique_ptr<IAgentProvider> provider) {
    if (!provider) return;
    // Re-parent onto service so lifetime is tied to this QObject.
    provider->setParent(this);
    if (running_) {
        connect(provider.get(), &IAgentProvider::event,
                this, &AgentSessionService::onProviderEvent,
                Qt::DirectConnection);
    }
    providers_.push_back(std::move(provider));
}

QStringList AgentSessionService::providerNames() const {
    QStringList out;
    for (const auto& p : providers_)
        out << p->name();
    return out;
}

QVariantList AgentSessionService::providerInfo() const {
    QVariantList out;
    for (const auto& p : providers_) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), p->name());
        m.insert(QStringLiteral("available"), p->available());
        m.insert(QStringLiteral("experimental"), p->experimental());
        out << m;
    }
    return out;
}

IAgentProvider* AgentSessionService::provider(const QString& name) const {
    for (const auto& p : providers_)
        if (p->name() == name)
            return p.get();
    return nullptr;
}

bool AgentSessionService::providerAvailable(const QString& name) const {
    auto* p = provider(name);
    return p && p->available();
}

QStringList AgentSessionService::allowedDirs() const {
    const std::string raw = config_.getStr(keys::AgentsAllowedDirs, "");
    QStringList parts = QString::fromStdString(raw).split(
        QLatin1Char(';'), Qt::SkipEmptyParts);
    QStringList out;
    for (QString p : parts) {
        p = p.trimmed();
        if (p.isEmpty()) continue;
        // Normalize to absolute cleaned path.
        QFileInfo fi(p);
        out << QDir::cleanPath(fi.exists() ? fi.absoluteFilePath() : p);
    }
    return out;
}

int AgentSessionService::maxConcurrent() const {
    return config_.getInt(keys::AgentsMaxConcurrent, 2);
}

int AgentSessionService::activeCount() const {
    int n = 0;
    for (auto it = live_.constBegin(); it != live_.constEnd(); ++it) {
        if (it->status == QLatin1String("working")
            || it->status == QLatin1String("needs_input"))
            ++n;
    }
    return n;
}

QString AgentSessionService::validateCwd(const QString& cwd) const {
    const QStringList allowed = allowedDirs();
    if (allowed.isEmpty()) {
        return QStringLiteral(
            "Spawning disabled: agents.allowed_dirs is empty. "
            "Ask the user to add an allowed directory in Settings ▸ Agents "
            "(monitor-only until then).");
    }
    if (cwd.trimmed().isEmpty()) {
        return QStringLiteral("cwd is required and must be inside agents.allowed_dirs.");
    }
    QFileInfo fi(cwd);
    const QString abs = QDir::cleanPath(fi.exists() ? fi.absoluteFilePath() : cwd);
    const QString absSlash = abs + QLatin1Char('/');
    for (const QString& root : allowed) {
        const QString r = QDir::cleanPath(root);
        if (abs.compare(r, Qt::CaseInsensitive) == 0)
            return {};
        const QString rSlash = r.endsWith(QLatin1Char('/')) ? r : (r + QLatin1Char('/'));
        if (absSlash.startsWith(rSlash, Qt::CaseInsensitive)
            || abs.startsWith(rSlash, Qt::CaseInsensitive))
            return {};
#ifdef Q_OS_WIN
        // Also accept if same drive-letter prefix match after lowercasing.
        if (abs.toLower().startsWith(r.toLower() + QLatin1Char('\\'))
            || abs.toLower() == r.toLower())
            return {};
#endif
    }
    return QStringLiteral(
        "cwd '%1' is outside agents.allowed_dirs. "
        "Ask the user to allow this path in Settings ▸ Agents.")
        .arg(cwd);
}

QString AgentSessionService::spawn(const QString& providerName, const QString& cwd,
                                   const QString& prompt, const QString& title) {
    last_error_.clear();
    const QString refuse = validateCwd(cwd);
    if (!refuse.isEmpty()) {
        last_error_ = refuse;
        PM_WARN("AgentSessionService::spawn refused: {}", refuse.toStdString());
        activity(QStringLiteral("agent_spawn"), refuse, false);
        return {};
    }
    if (prompt.trimmed().isEmpty()) {
        last_error_ = QStringLiteral("prompt is required");
        return {};
    }
    if (activeCount() >= maxConcurrent()) {
        last_error_ = QStringLiteral(
            "Concurrency cap reached (agents.max_concurrent=%1). "
            "Stop a session or raise the limit in Settings ▸ Agents.")
            .arg(maxConcurrent());
        activity(QStringLiteral("agent_spawn"), last_error_, false);
        return {};
    }

    IAgentProvider* p = provider(providerName);
    if (!p) {
        // Fall back: if name is a pty profile, use pty provider.
        p = provider(QStringLiteral("pty"));
        if (!p || (providerName != QLatin1String("pty") && providerName != p->name())) {
            // Still allow pty when providerName is an unknown profile name.
            if (!provider(QStringLiteral("pty"))) {
                last_error_ = QStringLiteral("Unknown provider '%1'").arg(providerName);
                return {};
            }
            p = provider(QStringLiteral("pty"));
        }
    }
    if (!p->available() && p->name() != QLatin1String("pty")) {
        last_error_ = QStringLiteral("Provider '%1' is not available (binary not on PATH)")
                          .arg(p->name());
        return {};
    }

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p->setSessionId(id);

    SpawnSpec spec;
    spec.provider = providerName.isEmpty() ? p->name() : providerName;
    spec.cwd = cwd;
    spec.prompt = prompt;
    spec.title = title.isEmpty()
        ? QStringLiteral("%1 session").arg(p->name())
        : title;

    const QString got = p->spawn(spec);
    if (got.isEmpty()) {
        last_error_ = QStringLiteral("Provider '%1' failed to spawn").arg(p->name());
        activity(QStringLiteral("agent_spawn"), last_error_, false);
        return {};
    }

    SessionRow row;
    row.id = got;
    row.provider = p->name();
    row.title = spec.title;
    row.cwd = cwd;
    row.status = QStringLiteral("working");
    row.created_at = to_unix(Clock::now());
    row.updated_at = row.created_at;
    row.last_message = prompt.left(200);
    live_.insert(row.id, row);
    upsertSession(row);
    activity(QStringLiteral("agent_spawn"),
             QStringLiteral("%1 in %2").arg(row.provider, row.cwd), true);

    // Ensure Started event is reflected even if provider didn't emit yet.
    AgentEvent started;
    started.session_id = row.id;
    started.kind = AgentEvent::Started;
    started.text = QStringLiteral("started");
    started.ts = QDateTime::currentMSecsSinceEpoch();
    publishBus(started);
    return row.id;
}

void AgentSessionService::send(const QString& id, const QString& text) {
    last_error_.clear();
    auto it = live_.find(id);
    SessionRow row;
    if (it == live_.end()) {
        // Try DB
        bool found = false;
        db_.query(
            "SELECT id,provider,title,cwd,status,native_session_id,cost_usd,"
            "created_at,updated_at,last_message FROM agent_sessions WHERE id=?1",
            {id.toStdString()},
            [&](const Row& r) {
                row.id = QString::fromStdString(r.text(0));
                row.provider = QString::fromStdString(r.text(1));
                row.title = QString::fromStdString(r.text(2));
                row.cwd = QString::fromStdString(r.text(3));
                row.status = QString::fromStdString(r.text(4));
                row.native_session_id = QString::fromStdString(r.text(5));
                row.cost_usd = r.dbl(6);
                row.created_at = r.i64(7);
                row.updated_at = r.i64(8);
                row.last_message = QString::fromStdString(r.text(9));
                found = true;
            });
        if (!found) {
            last_error_ = QStringLiteral("Unknown session '%1'").arg(id);
            return;
        }
        live_.insert(id, row);
        it = live_.find(id);
    } else {
        row = *it;
    }

    IAgentProvider* p = provider(row.provider);
    if (!p) {
        last_error_ = QStringLiteral("Provider '%1' gone").arg(row.provider);
        return;
    }
    // Resume uses native id when available.
    if (!row.native_session_id.isEmpty()) {
        // Providers read native from their slot; ensure spawn path has it via send.
    }
    p->send(id, text);
    it->status = QStringLiteral("working");
    it->last_message = text.left(200);
    it->updated_at = to_unix(Clock::now());
    upsertSession(*it);
    activity(QStringLiteral("agent_send"), id, true);
}

void AgentSessionService::stop(const QString& id) {
    last_error_.clear();
    auto it = live_.find(id);
    QString prov;
    if (it != live_.end())
        prov = it->provider;
    else {
        db_.query("SELECT provider FROM agent_sessions WHERE id=?1", {id.toStdString()},
                  [&](const Row& r) { prov = QString::fromStdString(r.text(0)); });
    }
    if (IAgentProvider* p = provider(prov))
        p->stop(id);

    SessionRow row;
    if (it != live_.end()) {
        row = *it;
    } else {
        row.id = id;
        row.provider = prov;
        row.created_at = to_unix(Clock::now());
    }
    row.status = QStringLiteral("stopped");
    row.updated_at = to_unix(Clock::now());
    row.last_message = QStringLiteral("stopped");
    live_.insert(id, row);
    upsertSession(row);

    AgentEvent e;
    e.session_id = id;
    e.kind = AgentEvent::Result;
    e.text = QStringLiteral("stopped");
    e.raw_json = QStringLiteral("{\"polymath_stopped\":true}");
    e.ts = QDateTime::currentMSecsSinceEpoch();
    publishBus(e);
    activity(QStringLiteral("agent_stop"), id, true);
}

QVariantMap AgentSessionService::status(const QString& id) const {
    QVariantMap out;
    if (!id.isEmpty()) {
        auto it = live_.constFind(id);
        SessionRow row;
        bool ok = false;
        if (it != live_.constEnd()) {
            row = *it;
            ok = true;
        } else {
            db_.query(
                "SELECT id,provider,title,cwd,status,native_session_id,cost_usd,"
                "created_at,updated_at,last_message FROM agent_sessions WHERE id=?1",
                {id.toStdString()},
                [&](const Row& r) {
                    row.id = QString::fromStdString(r.text(0));
                    row.provider = QString::fromStdString(r.text(1));
                    row.title = QString::fromStdString(r.text(2));
                    row.cwd = QString::fromStdString(r.text(3));
                    row.status = QString::fromStdString(r.text(4));
                    row.native_session_id = QString::fromStdString(r.text(5));
                    row.cost_usd = r.dbl(6);
                    row.created_at = r.i64(7);
                    row.updated_at = r.i64(8);
                    row.last_message = QString::fromStdString(r.text(9));
                    ok = true;
                });
        }
        if (!ok) {
            out.insert(QStringLiteral("error"), QStringLiteral("not found"));
            return out;
        }
        out.insert(QStringLiteral("id"), row.id);
        out.insert(QStringLiteral("provider"), row.provider);
        out.insert(QStringLiteral("title"), row.title);
        out.insert(QStringLiteral("cwd"), row.cwd);
        out.insert(QStringLiteral("status"), row.status);
        out.insert(QStringLiteral("costUsd"), row.cost_usd);
        out.insert(QStringLiteral("lastMessage"), row.last_message);
        out.insert(QStringLiteral("createdAt"), static_cast<qint64>(row.created_at));
        out.insert(QStringLiteral("updatedAt"), static_cast<qint64>(row.updated_at));
        return out;
    }

    // All summaries
    QVariantList sessions;
    for (const auto& m : listSessions(100))
        sessions << m;
    out.insert(QStringLiteral("sessions"), sessions);
    out.insert(QStringLiteral("active"), activeCount());
    out.insert(QStringLiteral("maxConcurrent"), maxConcurrent());
    return out;
}

QVariantList AgentSessionService::listSessions(int limit) const {
    QVariantList out;
    db_.query(
        "SELECT id,provider,title,cwd,status,native_session_id,cost_usd,"
        "created_at,updated_at,last_message FROM agent_sessions "
        "ORDER BY updated_at DESC LIMIT ?1",
        {limit},
        [&](const Row& r) {
            QVariantMap m;
            m.insert(QStringLiteral("id"), QString::fromStdString(r.text(0)));
            m.insert(QStringLiteral("provider"), QString::fromStdString(r.text(1)));
            m.insert(QStringLiteral("title"), QString::fromStdString(r.text(2)));
            m.insert(QStringLiteral("cwd"), QString::fromStdString(r.text(3)));
            m.insert(QStringLiteral("status"), QString::fromStdString(r.text(4)));
            m.insert(QStringLiteral("nativeSessionId"), QString::fromStdString(r.text(5)));
            m.insert(QStringLiteral("costUsd"), r.dbl(6));
            m.insert(QStringLiteral("createdAt"), static_cast<qint64>(r.i64(7)));
            m.insert(QStringLiteral("updatedAt"), static_cast<qint64>(r.i64(8)));
            m.insert(QStringLiteral("lastMessage"), QString::fromStdString(r.text(9)));
            out << m;
        });
    return out;
}

void AgentSessionService::upsertSession(const SessionRow& row) {
    db_.exec(
        "INSERT INTO agent_sessions(id,provider,title,cwd,status,native_session_id,"
        "cost_usd,created_at,updated_at,last_message) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10) "
        "ON CONFLICT(id) DO UPDATE SET "
        "provider=excluded.provider, title=excluded.title, cwd=excluded.cwd, "
        "status=excluded.status, native_session_id=excluded.native_session_id, "
        "cost_usd=excluded.cost_usd, updated_at=excluded.updated_at, "
        "last_message=excluded.last_message",
        {row.id.toStdString(), row.provider.toStdString(), row.title.toStdString(),
         row.cwd.toStdString(), row.status.toStdString(),
         row.native_session_id.toStdString(), row.cost_usd,
         row.created_at, row.updated_at, row.last_message.toStdString()});
}

QString AgentSessionService::statusForKind(AgentEvent::Kind k, const QString& text) const {
    switch (k) {
    case AgentEvent::NeedsInput:
    case AgentEvent::PermissionRequest:
        return QStringLiteral("needs_input");
    case AgentEvent::Error:
        return QStringLiteral("error");
    case AgentEvent::Result:
        if (text.contains(QLatin1String("stopped"), Qt::CaseInsensitive))
            return QStringLiteral("stopped");
        return QStringLiteral("done");
    case AgentEvent::Started:
    case AgentEvent::Thinking:
    case AgentEvent::ToolUse:
    case AgentEvent::AssistantText:
    case AgentEvent::CostUpdate:
        return QStringLiteral("working");
    }
    return QStringLiteral("working");
}

void AgentSessionService::onProviderEvent(const AgentEvent& e) {
    updateFromEvent(e);
    publishBus(e);
    emit sessionEvent(e);
}

void AgentSessionService::updateFromEvent(const AgentEvent& e) {
    auto it = live_.find(e.session_id);
    SessionRow row;
    if (it != live_.end()) {
        row = *it;
    } else {
        row.id = e.session_id;
        row.provider = QStringLiteral("unknown");
        row.created_at = to_unix(Clock::now());
    }

    if (!e.native_session_id.isEmpty())
        row.native_session_id = e.native_session_id;
    if (e.cost_usd > 0)
        row.cost_usd = e.cost_usd;
    if (!e.text.isEmpty()
        && e.kind != AgentEvent::CostUpdate
        && e.kind != AgentEvent::Thinking) {
        row.last_message = e.text.left(400);
    }

    // Terminal / status transitions. CostUpdate alone doesn't flip working→done.
    if (e.kind == AgentEvent::CostUpdate) {
        // keep status
    } else if (e.kind == AgentEvent::AssistantText || e.kind == AgentEvent::ToolUse
               || e.kind == AgentEvent::Thinking) {
        if (row.status != QLatin1String("needs_input")
            && row.status != QLatin1String("done")
            && row.status != QLatin1String("error")
            && row.status != QLatin1String("stopped"))
            row.status = QStringLiteral("working");
    } else {
        row.status = statusForKind(e.kind, e.text);
    }
    // If raw marks polymath_stopped, force stopped.
    if (e.raw_json.contains(QLatin1String("polymath_stopped")))
        row.status = QStringLiteral("stopped");

    row.updated_at = to_unix(Clock::now());
    live_.insert(row.id, row);
    upsertSession(row);

    if (e.kind == AgentEvent::NeedsInput || e.kind == AgentEvent::PermissionRequest)
        maybeSpeakNeedsInput(row, e.text);
}

void AgentSessionService::publishBus(const AgentEvent& e) {
    AgentSessionEvent bus;
    bus.session_id = e.session_id;
    bus.kind = agentEventKindName(e.kind);
    bus.text = e.text;
    bus.raw_json = e.raw_json;
    bus.cost_usd = e.cost_usd;
    bus.ts = e.ts > 0 ? e.ts : QDateTime::currentMSecsSinceEpoch();
    EventBus::instance().publishAgentSessionEvent(bus);

    if (e.kind == AgentEvent::NeedsInput || e.kind == AgentEvent::PermissionRequest) {
        Notice n;
        n.level = QStringLiteral("warn");
        n.source = QStringLiteral("Agents");
        QString title = e.session_id.left(8);
        auto it = live_.constFind(e.session_id);
        if (it != live_.constEnd() && !it->title.isEmpty())
            title = it->title;
        n.message = QStringLiteral("%1 needs input: %2")
                        .arg(title, e.text.left(160));
        EventBus::instance().publishNotice(n);
    }
}

void AgentSessionService::maybeSpeakNeedsInput(const SessionRow& row, const QString& text) {
    // Default ON (seeded as "1"); master kill-switch does not gate TTS here.
    if (config_.getStr(keys::AgentsSpeakNeedsInput, "1") == "0")
        return;
    SpeakRequest s;
    s.text = QStringLiteral("%1 needs input on %2")
                 .arg(row.provider, row.title.isEmpty() ? row.id.left(8) : row.title);
    if (!text.isEmpty())
        s.text += QStringLiteral(": %1").arg(text.left(80));
    s.request_id = QStringLiteral("agent-needs-%1").arg(row.id);
    EventBus::instance().publishSpeak(s);
}

void AgentSessionService::activity(const QString& action, const QString& detail, bool ok) {
    ActivityLog log(db_);
    log.record(action.toStdString(), detail.toStdString(), ok);
}

} // namespace polymath
