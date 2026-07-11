#include "task_model.h"

#include "database.h"
#include "logging.h"

namespace polymath {

TaskModel::TaskModel(Database& db, QObject* parent)
    : QAbstractListModel(parent), db_(db) {}

int TaskModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(tasks_.size());
}

QVariant TaskModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= tasks_.size())
        return {};
    const Task& t = tasks_.at(index.row());
    switch (role) {
        case IdRole:        return static_cast<qlonglong>(t.id);
        case TypeRole:      return t.type;
        case StatusRole:    return t.status;
        case DetailRole:    return t.detail;
        case PriorityRole:  return t.priority;
        case CreatedAtRole: return static_cast<qlonglong>(t.created_at);
        case UpdatedAtRole: return static_cast<qlonglong>(t.updated_at);
        default:            return {};
    }
}

QHash<int, QByteArray> TaskModel::roleNames() const {
    return {
        {IdRole,        "taskId"},
        {TypeRole,      "type"},
        {StatusRole,    "status"},
        {DetailRole,    "detail"},
        {PriorityRole,  "priority"},
        {CreatedAtRole, "createdAt"},
        {UpdatedAtRole, "updatedAt"},
    };
}

void TaskModel::refresh() {
    beginResetModel();
    tasks_.clear();
    db_.query(
        "SELECT id,type,status,priority,created_at,updated_at FROM tasks "
        "ORDER BY (status IN ('queued','running')) DESC, "
        "priority DESC, updated_at DESC LIMIT 500",
        {},
        [&](const Row& r) {
            Task t;
            t.id         = r.i64(0);
            t.type       = QString::fromStdString(r.text(1));
            t.status     = QString::fromStdString(r.text(2));
            t.priority   = static_cast<int>(r.i64(3));
            t.created_at = r.i64(4);
            t.updated_at = r.i64(5);
            tasks_.push_back(std::move(t));
        });
    endResetModel();
}

int TaskModel::rowForId(int64_t id) const {
    for (int i = 0; i < tasks_.size(); ++i)
        if (tasks_.at(i).id == id) return i;
    return -1;
}

bool TaskModel::loadOne(int64_t id, Task& out) const {
    bool found = false;
    db_.query(
        "SELECT id,type,status,priority,created_at,updated_at FROM tasks WHERE id=?1",
        {id},
        [&](const Row& r) {
            out.id         = r.i64(0);
            out.type       = QString::fromStdString(r.text(1));
            out.status     = QString::fromStdString(r.text(2));
            out.priority   = static_cast<int>(r.i64(3));
            out.created_at = r.i64(4);
            out.updated_at = r.i64(5);
            found = true;
        });
    return found;
}

void TaskModel::onTaskUpdated(const TaskEvent& e) {
    const int row = rowForId(e.task_id);
    if (row >= 0) {
        Task& t = tasks_[row];
        if (!e.type.isEmpty())   t.type   = e.type;
        if (!e.status.isEmpty()) t.status = e.status;
        t.detail = e.detail;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {TypeRole, StatusRole, DetailRole});
        return;
    }

    // Unseen task: pull authoritative columns from the DB, falling back to the
    // event payload if the row is not committed yet.  New rows go to the top.
    Task t;
    if (!loadOne(e.task_id, t)) {
        t.id     = e.task_id;
        t.type   = e.type;
        t.status = e.status;
    }
    t.detail = e.detail;
    beginInsertRows({}, 0, 0);
    tasks_.insert(0, std::move(t));
    endInsertRows();
}

// ---------------------------------------------------------------------------
//  ScheduledGoalsModel (overhaul2 D1)
// ---------------------------------------------------------------------------

ScheduledGoalsModel::ScheduledGoalsModel(Database& db, QObject* parent)
    : QAbstractListModel(parent), db_(db) {}

int ScheduledGoalsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(schedules_.size());
}

QVariant ScheduledGoalsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= schedules_.size())
        return {};
    const Schedule& s = schedules_.at(index.row());
    switch (role) {
        case IdRole:       return static_cast<qlonglong>(s.id);
        case TitleRole:    return s.title;
        case KindRole:     return s.kind;
        case SpecRole:     return s.spec;
        case NextFireRole: return static_cast<qlonglong>(s.next_fire);
        case LastFireRole: return static_cast<qlonglong>(s.last_fire);
        case EnabledRole:  return s.enabled;
        case DeliverRole:  return s.deliver;
        case SkillRole:    return s.skill;
        case PromptRole:   return s.prompt;
        default:           return {};
    }
}

QHash<int, QByteArray> ScheduledGoalsModel::roleNames() const {
    return {
        {IdRole,       "scheduleId"},
        {TitleRole,    "title"},
        {KindRole,     "kind"},
        {SpecRole,     "spec"},
        {NextFireRole, "nextFire"},
        {LastFireRole, "lastFire"},
        {EnabledRole,  "enabled"},
        {DeliverRole,  "deliver"},
        {SkillRole,    "skill"},
        {PromptRole,   "prompt"},
    };
}

void ScheduledGoalsModel::refresh() {
    beginResetModel();
    schedules_.clear();
    db_.query(
        "SELECT id,title,kind,spec,next_fire,last_fire,enabled,deliver,skill,prompt "
        "FROM scheduled_goals "
        "ORDER BY (next_fire IS NULL), next_fire ASC, created_at DESC LIMIT 500",
        {},
        [&](const Row& r) {
            Schedule s;
            s.id         = r.i64(0);
            s.title      = QString::fromStdString(r.text(1));
            s.kind       = QString::fromStdString(r.text(2));
            s.spec       = QString::fromStdString(r.text(3));
            s.next_fire  = r.isNull(4) ? 0 : r.i64(4);
            s.last_fire  = r.isNull(5) ? 0 : r.i64(5);
            s.enabled    = r.i64(6) != 0;
            s.deliver    = QString::fromStdString(r.text(7));
            s.skill      = QString::fromStdString(r.text(8));
            s.prompt     = QString::fromStdString(r.text(9));
            schedules_.push_back(std::move(s));
        });
    endResetModel();
}

void ScheduledGoalsModel::setEnabled(int row, bool enabled) {
    if (row < 0 || row >= schedules_.size()) return;
    Schedule& s = schedules_[row];
    if (s.enabled == enabled) return;

    db_.exec("UPDATE scheduled_goals SET enabled=?1 WHERE id=?2",
             {enabled ? 1 : 0, static_cast<int64_t>(s.id)});
    s.enabled = enabled;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {EnabledRole});
    PM_INFO("ScheduledGoalsModel: schedule {} enabled={}", s.id, enabled);
}

void ScheduledGoalsModel::removeItem(int row) {
    if (row < 0 || row >= schedules_.size()) return;
    const int64_t id = schedules_.at(row).id;
    db_.exec("DELETE FROM scheduled_goals WHERE id=?1", {static_cast<int64_t>(id)});
    beginRemoveRows({}, row, row);
    schedules_.removeAt(row);
    endRemoveRows();
    PM_INFO("ScheduledGoalsModel: schedule {} deleted", id);
}

} // namespace polymath
