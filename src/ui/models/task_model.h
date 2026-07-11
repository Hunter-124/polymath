#pragma once
//
// TaskModel — a QAbstractListModel view over the deep-work `tasks` table, shown
// in TaskQueueView.  Refreshes from SQLite and stays current by listening to
// EventBus::taskUpdated (a TaskEvent carries id/type/status/detail).  When a
// task id we have not seen appears, the row is pulled from the DB on demand.
//
#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

#include "event_bus.h"

namespace polymath {

class Database;

class TaskModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TypeRole,
        StatusRole,
        DetailRole,
        PriorityRole,
        CreatedAtRole,
        UpdatedAtRole
    };

    explicit TaskModel(Database& db, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

public slots:
    // Queued connection from EventBus::taskUpdated (worker -> UI thread).
    void onTaskUpdated(const polymath::TaskEvent& e);

private:
    struct Task {
        int64_t id = 0;
        QString type;
        QString status;
        QString detail;
        int     priority = 0;
        int64_t created_at = 0;
        int64_t updated_at = 0;
    };

    int  rowForId(int64_t id) const;
    bool loadOne(int64_t id, Task& out) const;  // pull a single row from the DB

    Database&     db_;
    QVector<Task> tasks_;
};

// ScheduledGoalsModel — a QAbstractListModel view over `scheduled_goals`
// (overhaul2 D1 — Scheduler v2). Backs the Tasks ▸ Scheduled section. The
// table is the source of truth (schema.h); mutations here (enable toggle,
// delete) write through to SQLite directly, same pattern as ShoppingModel.
// Firing/rescheduling is owned entirely by ProactiveEngine — this model only
// displays + lets the user enable/disable/delete rows.
class ScheduledGoalsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        KindRole,
        SpecRole,
        NextFireRole,
        LastFireRole,
        EnabledRole,
        DeliverRole,
        SkillRole,
        PromptRole,
    };

    explicit ScheduledGoalsModel(Database& db, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Reload all rows from SQLite, next-fire first (disabled/never-scheduled last).
    Q_INVOKABLE void refresh();

    // Enable/disable the row at `row` (does not touch next_fire; ProactiveEngine
    // only fires enabled=1 rows).
    Q_INVOKABLE void setEnabled(int row, bool enabled);

    // Hard-delete the row at `row` (the Tasks UI's "trash" action; distinct
    // from cancel_schedule's soft-disable used by the agent tool).
    Q_INVOKABLE void removeItem(int row);

private:
    struct Schedule {
        int64_t id = 0;
        QString title;
        QString kind;
        QString spec;
        int64_t next_fire = 0;   // 0 => NULL (not scheduled)
        int64_t last_fire = 0;   // 0 => NULL (never fired)
        bool    enabled = true;
        QString deliver;
        QString skill;
        QString prompt;
    };

    Database&          db_;
    QVector<Schedule>  schedules_;
};

} // namespace polymath
