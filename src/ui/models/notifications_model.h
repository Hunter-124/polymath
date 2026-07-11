#pragma once
//
// NotificationsModel — in-memory notification center (cap 200).
// Context property "notifications". Spec: docs/overhaul/02 §Feature 3.
// C1: pending SafetyPolicy confirmations appear as category "confirm" with
// approveConfirm/denyConfirm invokables that publish ConfirmResponse.
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
        CategoryRole,
        // C1: true when this row is a pending safety confirmation (shows
        // approve/deny affordances in NotificationCenter when wired).
        PendingActionRole,
        // C1: ConfirmRequest id (same as IdRole for confirm rows; empty otherwise).
        ConfirmIdRole
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

    // C1: resolve a pending confirmation (same path as ConfirmDialog /
    // app.respondConfirm). Removes the matching notification row.
    Q_INVOKABLE void approveConfirm(const QString& confirmId);
    Q_INVOKABLE void denyConfirm(const QString& confirmId);

public slots:
    void onNotice(const polymath::Notice& n);
    void onTask(const polymath::TaskEvent& t);
    void onReminder(const polymath::ReminderFired& r);
    void onDetection(const polymath::Detection& d);
    void onGoalUpdate(const polymath::GoalUpdate& g);
    // C1: AgentLoop parked a tool call waiting_user.
    void onConfirmRequest(const polymath::ConfirmRequest& r);
    // C1: any path answered (dialog / voice / this model) — drop the row.
    void onConfirmResponse(const polymath::ConfirmResponse& r);

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
        QString category;   // notice|task|reminder|detection|goal|confirm
        bool    pending_action = false;  // C1 confirm rows until resolved
        QString confirm_id;              // ConfirmRequest id (empty if n/a)
    };

    void prepend(Row row);
    void removeByConfirmId(const QString& confirmId);
    void recomputeUnread();
    void respondConfirm(const QString& confirmId, bool approved);
    static QString formatTime(int64_t ts);
    static QString makeId();

    Database& db_;
    QVector<Row> rows_;
    int unread_ = 0;
    static constexpr int kCap = 200;
};

} // namespace polymath
