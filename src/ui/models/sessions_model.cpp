#include "sessions_model.h"
#include "agent_session_service.h"
#include "database.h"

#include <QDateTime>
#include <QMetaObject>
#include <QThread>

namespace polymath {

SessionsModel::SessionsModel(Database& db, QObject* parent)
    : QAbstractListModel(parent), db_(db) {}

void SessionsModel::setService(AgentSessionService* svc) {
    service_ = svc;
}

int SessionsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(rows_.size());
}

QVariant SessionsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size())
        return {};
    const Session& s = rows_.at(index.row());
    switch (role) {
    case IdRole:           return s.id;
    case ProviderRole:     return s.provider;
    case TitleRole:        return s.title;
    case CwdRole:          return s.cwd;
    case StatusRole:       return s.status;
    case LastMessageRole:  return s.lastMessage;
    case CostUsdRole:      return s.costUsd;
    case ElapsedRole:      return formatElapsed(s.createdAt);
    case UnreadPingRole:   return s.unreadPing;
    case ExperimentalRole: return s.experimental;
    case CreatedAtRole:    return static_cast<qint64>(s.createdAt);
    case UpdatedAtRole:    return static_cast<qint64>(s.updatedAt);
    default:               return {};
    }
}

QHash<int, QByteArray> SessionsModel::roleNames() const {
    return {
        {IdRole,           "sessionId"},
        {ProviderRole,     "provider"},
        {TitleRole,        "title"},
        {CwdRole,          "cwd"},
        {StatusRole,       "status"},
        {LastMessageRole,  "lastMessage"},
        {CostUsdRole,      "costUsd"},
        {ElapsedRole,      "elapsed"},
        {UnreadPingRole,   "unreadPing"},
        {ExperimentalRole, "experimental"},
        {CreatedAtRole,    "createdAt"},
        {UpdatedAtRole,    "updatedAt"},
    };
}

QString SessionsModel::formatElapsed(qint64 createdAtSec) {
    if (createdAtSec <= 0)
        return QStringLiteral("—");
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 sec = qMax<qint64>(0, now - createdAtSec);
    if (sec < 60)
        return QStringLiteral("%1s").arg(sec);
    if (sec < 3600)
        return QStringLiteral("%1m").arg(sec / 60);
    return QStringLiteral("%1h %2m").arg(sec / 3600).arg((sec % 3600) / 60);
}

int SessionsModel::rowForId(const QString& id) const {
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_.at(i).id == id) return i;
    return -1;
}

void SessionsModel::refresh() {
    beginResetModel();
    rows_.clear();
    // Prefer service list (authoritative after ensureSchema); fall back to raw SQL.
    if (service_) {
        // Service may live on another thread — listSessions is const DB read and
        // is mutex-guarded; safe enough from UI for a refresh snapshot.
        const QVariantList list = service_->listSessions(200);
        for (const QVariant& v : list) {
            const QVariantMap m = v.toMap();
            Session s;
            s.id = m.value(QStringLiteral("id")).toString();
            s.provider = m.value(QStringLiteral("provider")).toString();
            s.title = m.value(QStringLiteral("title")).toString();
            s.cwd = m.value(QStringLiteral("cwd")).toString();
            s.status = m.value(QStringLiteral("status")).toString();
            s.lastMessage = m.value(QStringLiteral("lastMessage")).toString();
            s.costUsd = m.value(QStringLiteral("costUsd")).toDouble();
            s.createdAt = m.value(QStringLiteral("createdAt")).toLongLong();
            s.updatedAt = m.value(QStringLiteral("updatedAt")).toLongLong();
            s.experimental = (s.provider == QLatin1String("codex"));
            rows_.push_back(std::move(s));
        }
    } else {
        // Capture/tests without service: try table if present.
        try {
            db_.query(
                "SELECT id,provider,title,cwd,status,cost_usd,created_at,updated_at,"
                "last_message FROM agent_sessions ORDER BY updated_at DESC LIMIT 200",
                {},
                [this](const Row& r) {
                    Session s;
                    s.id = QString::fromStdString(r.text(0));
                    s.provider = QString::fromStdString(r.text(1));
                    s.title = QString::fromStdString(r.text(2));
                    s.cwd = QString::fromStdString(r.text(3));
                    s.status = QString::fromStdString(r.text(4));
                    s.costUsd = r.dbl(5);
                    s.createdAt = r.i64(6);
                    s.updatedAt = r.i64(7);
                    s.lastMessage = QString::fromStdString(r.text(8));
                    s.experimental = (s.provider == QLatin1String("codex"));
                    rows_.push_back(std::move(s));
                });
        } catch (...) {
            // table may not exist yet
        }
    }
    endResetModel();
    emit countChanged();
}

QString SessionsModel::spawn(const QString& provider, const QString& cwd,
                             const QString& prompt, const QString& title) {
    last_error_.clear();
    if (!service_) {
        last_error_ = QStringLiteral("sessions service not ready");
        emit spawnFailed(last_error_);
        return {};
    }
    // Marshal onto service thread if needed.
    QString id;
    if (service_->thread() == QThread::currentThread()) {
        id = service_->spawn(provider, cwd, prompt, title);
    } else {
        QMetaObject::invokeMethod(service_, [&]() {
            id = service_->spawn(provider, cwd, prompt, title);
        }, Qt::BlockingQueuedConnection);
    }
    if (id.isEmpty()) {
        last_error_ = service_->lastError();
        emit spawnFailed(last_error_);
        return {};
    }
    // Optimistic row; events will refine status.
    Session s;
    s.id = id;
    s.provider = provider;
    s.title = title.isEmpty() ? QStringLiteral("%1 session").arg(provider) : title;
    s.cwd = cwd;
    s.status = QStringLiteral("working");
    s.lastMessage = prompt.left(200);
    s.createdAt = QDateTime::currentSecsSinceEpoch();
    s.updatedAt = s.createdAt;
    s.experimental = (provider == QLatin1String("codex"));
    beginInsertRows({}, 0, 0);
    rows_.prepend(std::move(s));
    endInsertRows();
    emit countChanged();
    return id;
}

void SessionsModel::send(const QString& id, const QString& text) {
    if (!service_) return;
    if (service_->thread() == QThread::currentThread()) {
        service_->send(id, text);
    } else {
        QMetaObject::invokeMethod(service_, [this, id, text]() {
            service_->send(id, text);
        }, Qt::QueuedConnection);
    }
    const int row = rowForId(id);
    if (row >= 0) {
        rows_[row].status = QStringLiteral("working");
        rows_[row].lastMessage = text.left(200);
        rows_[row].unreadPing = false;
        rows_[row].updatedAt = QDateTime::currentSecsSinceEpoch();
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {StatusRole, LastMessageRole, UnreadPingRole,
                                    UpdatedAtRole, ElapsedRole});
    }
}

void SessionsModel::stop(const QString& id) {
    if (!service_) return;
    if (service_->thread() == QThread::currentThread()) {
        service_->stop(id);
    } else {
        QMetaObject::invokeMethod(service_, [this, id]() {
            service_->stop(id);
        }, Qt::QueuedConnection);
    }
    const int row = rowForId(id);
    if (row >= 0) {
        rows_[row].status = QStringLiteral("stopped");
        rows_[row].lastMessage = QStringLiteral("stopped");
        rows_[row].updatedAt = QDateTime::currentSecsSinceEpoch();
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {StatusRole, LastMessageRole, UpdatedAtRole});
    }
}

void SessionsModel::clearPing(const QString& id) {
    const int row = rowForId(id);
    if (row < 0) return;
    if (!rows_[row].unreadPing) return;
    rows_[row].unreadPing = false;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {UnreadPingRole});
}

QVariantList SessionsModel::availableProviders() const {
    if (service_)
        return service_->providerInfo();
    // Stub for capture without service.
    return {
        QVariantMap{{QStringLiteral("name"), QStringLiteral("claude-code")},
                    {QStringLiteral("available"), true},
                    {QStringLiteral("experimental"), false}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("codex")},
                    {QStringLiteral("available"), false},
                    {QStringLiteral("experimental"), true}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("pty")},
                    {QStringLiteral("available"), true},
                    {QStringLiteral("experimental"), false}},
    };
}

QStringList SessionsModel::eventLog(const QString& id) const {
    const int row = rowForId(id);
    if (row < 0) return {};
    return rows_.at(row).events;
}

void SessionsModel::applyEvent(Session& s, const AgentSessionEvent& e) {
    if (!e.text.isEmpty()
        && e.kind != QLatin1String("CostUpdate")
        && e.kind != QLatin1String("Thinking")) {
        s.lastMessage = e.text.left(400);
    }
    if (e.cost_usd > 0)
        s.costUsd = e.cost_usd;
    s.updatedAt = e.ts > 1'000'000'000'000LL
        ? e.ts / 1000   // ms → sec
        : (e.ts > 0 ? e.ts : QDateTime::currentSecsSinceEpoch());

    if (e.kind == QLatin1String("NeedsInput")
        || e.kind == QLatin1String("PermissionRequest")) {
        s.status = QStringLiteral("needs_input");
        s.unreadPing = true;
    } else if (e.kind == QLatin1String("Error")) {
        s.status = QStringLiteral("error");
    } else if (e.kind == QLatin1String("Result")) {
        if (e.text.contains(QLatin1String("stopped"), Qt::CaseInsensitive)
            || e.raw_json.contains(QLatin1String("polymath_stopped")))
            s.status = QStringLiteral("stopped");
        else
            s.status = QStringLiteral("done");
    } else if (e.kind == QLatin1String("Started")
            || e.kind == QLatin1String("Thinking")
            || e.kind == QLatin1String("ToolUse")
            || e.kind == QLatin1String("AssistantText")) {
        if (s.status != QLatin1String("needs_input")
            && s.status != QLatin1String("done")
            && s.status != QLatin1String("error")
            && s.status != QLatin1String("stopped"))
            s.status = QStringLiteral("working");
    }
    // CostUpdate: leave status alone.

    QString line = e.kind;
    if (!e.text.isEmpty())
        line += QStringLiteral(": ") + e.text.left(200);
    else if (!e.raw_json.isEmpty())
        line += QStringLiteral(" ") + e.raw_json.left(120);
    s.events.prepend(line);
    while (s.events.size() > 100)
        s.events.removeLast();
}

void SessionsModel::onAgentSessionEvent(const AgentSessionEvent& e) {
    const int row = rowForId(e.session_id);
    if (row >= 0) {
        applyEvent(rows_[row], e);
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {StatusRole, LastMessageRole, CostUsdRole,
                                    ElapsedRole, UnreadPingRole, UpdatedAtRole});
        // Promote needs_input / working to top.
        if (row > 0 && (rows_[row].status == QLatin1String("needs_input")
                        || rows_[row].status == QLatin1String("working"))) {
            beginMoveRows({}, row, row, {}, 0);
            rows_.move(row, 0);
            endMoveRows();
        }
        return;
    }

    // Unseen session — create a row from the event.
    Session s;
    s.id = e.session_id;
    s.status = QStringLiteral("working");
    s.createdAt = QDateTime::currentSecsSinceEpoch();
    applyEvent(s, e);
    beginInsertRows({}, 0, 0);
    rows_.prepend(std::move(s));
    endInsertRows();
    emit countChanged();
}

} // namespace polymath
