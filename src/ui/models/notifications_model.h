#pragma once
//
// NotificationsModel — in-memory notification center (cap 200).
// Context property "notifications". Spec: docs/overhaul/02 §Feature 3.
//
#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>
#include <cstdint>

#include "event_bus.h"

namespace polymath {

class Database;

class NotificationsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        SeverityRole,
        SourceRole,
        TitleRole,
        BodyRole,
        TimestampRole,
        TimeLabelRole,
        ReadRole,
        CategoryRole
    };

    explicit NotificationsModel(Database& db, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int unreadCount() const { return unread_; }

    Q_INVOKABLE void markAllRead();
    Q_INVOKABLE void markRead(const QString& id);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void refreshFromEvents();

public slots:
    void onNotice(const polymath::Notice& n);
    void onTask(const polymath::TaskEvent& t);
    void onReminder(const polymath::ReminderFired& r);
    void onDetection(const polymath::Detection& d);
    void onGoalUpdate(const polymath::GoalUpdate& g);

signals:
    void unreadCountChanged();

private:
    struct Row {
        QString id;
        QString severity;   // info|warn|error|good
        QString source;
        QString title;
        QString body;
        int64_t timestamp = 0;
        QString timeLabel;
        bool    read = false;
        QString category;   // notice|task|reminder|detection|goal
    };

    void prepend(Row row);
    void recomputeUnread();
    static QString formatTime(int64_t ts);
    static QString makeId();

    Database& db_;
    QVector<Row> rows_;
    int unread_ = 0;
    static constexpr int kCap = 200;
};

} // namespace polymath
