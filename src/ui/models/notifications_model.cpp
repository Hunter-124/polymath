#include "notifications_model.h"
#include "database.h"

#include <QDateTime>
#include <QUuid>

namespace polymath {

NotificationsModel::NotificationsModel(Database& db, QObject* parent)
    : QAbstractListModel(parent), db_(db) {}

int NotificationsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return rows_.size();
}

QVariant NotificationsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size())
        return {};
    const auto& r = rows_.at(index.row());
    switch (role) {
    case IdRole:           return r.id;
    case SeverityRole:     return r.severity;
    case SourceRole:       return r.source;
    case TitleRole:        return r.title;
    case BodyRole:         return r.body;
    case TimestampRole:    return static_cast<qint64>(r.timestamp);
    case TimeLabelRole:    return r.timeLabel;
    case ReadRole:         return r.read;
    case CategoryRole:     return r.category;
    case PendingActionRole: return r.pending_action;
    case ConfirmIdRole:    return r.confirm_id;
    default:               return {};
    }
}

QHash<int, QByteArray> NotificationsModel::roleNames() const {
    return {
        {IdRole,            "id"},
        {SeverityRole,      "severity"},
        {SourceRole,        "source"},
        {TitleRole,         "title"},
        {BodyRole,          "body"},
        {TimestampRole,     "timestamp"},
        {TimeLabelRole,     "timeLabel"},
        {ReadRole,          "read"},
        {CategoryRole,      "category"},
        {PendingActionRole, "pendingAction"},
        {ConfirmIdRole,     "confirmId"},
    };
}

QString NotificationsModel::formatTime(int64_t ts) {
    return QDateTime::fromSecsSinceEpoch(ts).toLocalTime().toString(QStringLiteral("HH:mm"));
}

QString NotificationsModel::makeId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void NotificationsModel::recomputeUnread() {
    int n = 0;
    for (const auto& r : rows_) if (!r.read) ++n;
    if (n != unread_) {
        unread_ = n;
        emit unreadCountChanged();
    }
}

void NotificationsModel::prepend(Row row) {
    beginInsertRows({}, 0, 0);
    rows_.prepend(std::move(row));
    endInsertRows();
    while (rows_.size() > kCap) {
        const int last = rows_.size() - 1;
        beginRemoveRows({}, last, last);
        rows_.removeLast();
        endRemoveRows();
    }
    recomputeUnread();
}

void NotificationsModel::removeByConfirmId(const QString& confirmId) {
    if (confirmId.isEmpty()) return;
    for (int i = 0; i < rows_.size(); ++i) {
        if (rows_[i].confirm_id == confirmId ||
            (rows_[i].category == QLatin1String("confirm") && rows_[i].id == confirmId)) {
            beginRemoveRows({}, i, i);
            rows_.removeAt(i);
            endRemoveRows();
            recomputeUnread();
            return;
        }
    }
}

void NotificationsModel::markAllRead() {
    if (rows_.isEmpty()) return;
    for (auto& r : rows_) r.read = true;
    emit dataChanged(index(0), index(rows_.size() - 1), {ReadRole});
    recomputeUnread();
}

void NotificationsModel::markRead(const QString& id) {
    for (int i = 0; i < rows_.size(); ++i) {
        if (rows_[i].id == id) {
            if (!rows_[i].read) {
                rows_[i].read = true;
                emit dataChanged(index(i), index(i), {ReadRole});
                recomputeUnread();
            }
            return;
        }
    }
}

void NotificationsModel::clearAll() {
    if (rows_.isEmpty()) return;
    beginResetModel();
    rows_.clear();
    endResetModel();
    recomputeUnread();
}

void NotificationsModel::refreshFromEvents() {
    // Seed last ~50 rows from the existing events table (ActivityLog).
    // Schema: events(kind, camera_id, user_id, label, thumb_path, ts)
    beginResetModel();
    rows_.clear();
    try {
        db_.query(
            "SELECT kind, label, ts FROM events ORDER BY ts DESC LIMIT 50", {},
            [this](const polymath::Row& dbRow) {
                Row r;
                r.id = makeId();
                r.category = QStringLiteral("event");
                r.source = QString::fromStdString(dbRow.text(0));
                r.severity = QStringLiteral("info");
                r.title = QString::fromStdString(dbRow.text(0));
                r.body = QString::fromStdString(dbRow.text(1));
                r.timestamp = dbRow.i64(2);
                if (r.timestamp <= 0)
                    r.timestamp = QDateTime::currentSecsSinceEpoch();
                r.timeLabel = formatTime(r.timestamp);
                r.read = true;  // historical seed starts read
                rows_.push_back(std::move(r));
            });
    } catch (...) {
        // Table may be empty / missing on very fresh DBs — stay empty.
    }
    endResetModel();
    recomputeUnread();
}

void NotificationsModel::respondConfirm(const QString& confirmId, bool approved) {
    if (confirmId.isEmpty()) return;
    ConfirmResponse resp;
    resp.id = confirmId;
    resp.approved = approved;
    resp.always_allow = false;
    EventBus::instance().publishConfirmResponse(resp);
    // Row removal also arrives via onConfirmResponse; do it eagerly so the
    // center updates before the queued bus hop returns to this object.
    removeByConfirmId(confirmId);
}

void NotificationsModel::approveConfirm(const QString& confirmId) {
    respondConfirm(confirmId, true);
}

void NotificationsModel::denyConfirm(const QString& confirmId) {
    respondConfirm(confirmId, false);
}

void NotificationsModel::onNotice(const Notice& n) {
    Row r;
    r.id = makeId();
    r.category = QStringLiteral("notice");
    r.severity = n.level.isEmpty() ? QStringLiteral("info") : n.level;
    r.source = n.source;
    r.title = n.source.isEmpty() ? QStringLiteral("Notice") : n.source;
    r.body = n.message;
    r.timestamp = QDateTime::currentSecsSinceEpoch();
    r.timeLabel = formatTime(r.timestamp);
    r.read = false;
    prepend(std::move(r));
}

void NotificationsModel::onTask(const TaskEvent& t) {
    Row r;
    r.id = makeId();
    r.category = QStringLiteral("task");
    r.severity = (t.status == QLatin1String("error") || t.status == QLatin1String("failed"))
                 ? QStringLiteral("error")
                 : (t.status == QLatin1String("done") ? QStringLiteral("good")
                                                      : QStringLiteral("info"));
    r.source = QStringLiteral("tasks");
    r.title = t.type.isEmpty() ? QStringLiteral("Task") : t.type;
    r.body = QStringLiteral("%1 — %2").arg(t.status, t.detail);
    r.timestamp = QDateTime::currentSecsSinceEpoch();
    r.timeLabel = formatTime(r.timestamp);
    r.read = false;
    prepend(std::move(r));
}

void NotificationsModel::onReminder(const ReminderFired& rem) {
    Row r;
    r.id = makeId();
    r.category = QStringLiteral("reminder");
    r.severity = QStringLiteral("warn");
    r.source = QStringLiteral("reminders");
    r.title = QStringLiteral("Reminder");
    r.body = rem.text;
    r.timestamp = QDateTime::currentSecsSinceEpoch();
    r.timeLabel = formatTime(r.timestamp);
    r.read = false;
    prepend(std::move(r));
}

void NotificationsModel::onDetection(const Detection& d) {
    Row r;
    r.id = makeId();
    r.category = QStringLiteral("detection");
    r.severity = QStringLiteral("info");
    r.source = QStringLiteral("vision");
    r.title = QStringLiteral("Camera %1").arg(d.camera_id);
    QStringList labels;
    for (const auto& b : d.boxes) {
        if (!b.label.empty()) labels << QString::fromStdString(b.label);
    }
    r.body = labels.isEmpty() ? QStringLiteral("Detection")
                              : labels.join(QStringLiteral(", "));
    r.timestamp = QDateTime::currentSecsSinceEpoch();
    r.timeLabel = formatTime(r.timestamp);
    r.read = false;
    prepend(std::move(r));
}

void NotificationsModel::onGoalUpdate(const GoalUpdate& g) {
    Row r;
    r.id = makeId();
    r.category = QStringLiteral("goal");
    r.severity = (g.status == QLatin1String("failed")) ? QStringLiteral("error")
                : (g.status == QLatin1String("done") ? QStringLiteral("good")
                                                     : QStringLiteral("info"));
    r.source = QStringLiteral("agent");
    r.title = g.title.isEmpty() ? QStringLiteral("Goal") : g.title;
    r.body = QStringLiteral("%1 — %2").arg(g.status, g.summary);
    r.timestamp = QDateTime::currentSecsSinceEpoch();
    r.timeLabel = formatTime(r.timestamp);
    r.read = false;
    prepend(std::move(r));
}

void NotificationsModel::onConfirmRequest(const ConfirmRequest& req) {
    if (req.id.isEmpty()) return;
    // Replace any existing pending row for the same confirm id (re-publish).
    removeByConfirmId(req.id);

    Row r;
    // Use the ConfirmRequest id as the notification id so approve/deny from
    // the center can pass model.id straight through.
    r.id = req.id;
    r.category = QStringLiteral("confirm");
    r.severity = QStringLiteral("warn");
    r.source = QStringLiteral("safety");
    r.title = req.tool.isEmpty()
                  ? QStringLiteral("Needs your approval")
                  : QStringLiteral("Needs approval: %1").arg(req.tool);
    r.body = req.summary.isEmpty() ? req.reason : req.summary;
    r.timestamp = QDateTime::currentSecsSinceEpoch();
    r.timeLabel = formatTime(r.timestamp);
    r.read = false;
    r.pending_action = true;
    r.confirm_id = req.id;
    prepend(std::move(r));
}

void NotificationsModel::onConfirmResponse(const ConfirmResponse& resp) {
    removeByConfirmId(resp.id);
}

} // namespace polymath
