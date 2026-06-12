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
    // Live status tallies for the queue header chips / dashboard.
    Q_PROPERTY(int queuedCount  READ queuedCount  NOTIFY countsChanged)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY countsChanged)
    Q_PROPERTY(int doneCount    READ doneCount    NOTIFY countsChanged)
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

    int queuedCount() const  { return countByStatus(QLatin1String("queued")); }
    int runningCount() const { return countByStatus(QLatin1String("running")); }
    int doneCount() const    { return countByStatus(QLatin1String("done")); }

signals:
    void countsChanged();

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
    int  countByStatus(QLatin1String status) const;

    Database&     db_;
    QVector<Task> tasks_;
};

} // namespace polymath
