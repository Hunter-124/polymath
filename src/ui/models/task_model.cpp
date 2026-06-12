#include "task_model.h"

#include "database.h"

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
    emit countsChanged();
}

int TaskModel::rowForId(int64_t id) const {
    for (int i = 0; i < tasks_.size(); ++i)
        if (tasks_.at(i).id == id) return i;
    return -1;
}

int TaskModel::countByStatus(QLatin1String status) const {
    int n = 0;
    for (const Task& t : tasks_) n += t.status == status ? 1 : 0;
    return n;
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
        emit countsChanged();
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
    emit countsChanged();
}

} // namespace polymath
