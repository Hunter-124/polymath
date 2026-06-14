#pragma once
//
// LabModel — a QAbstractListModel over the `lab_sessions` table for the desktop
// Lab cockpit (LabView).  Each row carries the session plus computed step tallies
// (how many steps, how many verified).  It refreshes from SQLite and stays live by
// listening to EventBus::labStep (emitted as the guided lab agent advances a
// session): any step event refreshes the affected session's tallies and surfaces
// the current prompt/status as a banner the view can show.
//
#include <QAbstractListModel>
#include <QString>
#include <QVariantList>
#include <QVector>

#include "event_bus.h"

namespace polymath {

class Database;

class LabModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countsChanged)
    // The most recent live step event, for the "current step" banner.
    Q_PROPERTY(qlonglong liveSessionId READ liveSessionId NOTIFY liveStepChanged)
    Q_PROPERTY(QString livePrompt READ livePrompt NOTIFY liveStepChanged)
    Q_PROPERTY(QString liveStatus READ liveStatus NOTIFY liveStepChanged)
public:
    enum Roles {
        SessionIdRole = Qt::UserRole + 1,
        TitleRole,
        ObjectiveRole,
        StatusRole,
        StartedAtRole,
        ReportDocIdRole,
        StepCountRole,
        VerifiedCountRole,
    };

    explicit LabModel(Database& db, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    // The steps of one session, as row maps for a Repeater in the expanded view.
    Q_INVOKABLE QVariantList steps(qlonglong sessionId) const;

    int       activeCount() const;
    qlonglong liveSessionId() const { return live_session_; }
    QString   livePrompt() const { return live_prompt_; }
    QString   liveStatus() const { return live_status_; }

signals:
    void countsChanged();
    void liveStepChanged();

public slots:
    // Queued connection from EventBus::labStep (agent worker -> UI thread).
    void onLabStep(const polymath::LabStepEvent& e);

private:
    struct Session {
        int64_t id = 0;
        QString title;
        QString objective;
        QString status;
        int64_t started_at = 0;
        int64_t report_doc_id = 0;
        int     step_count = 0;
        int     verified_count = 0;
    };

    Database&        db_;
    QVector<Session> sessions_;
    int64_t          live_session_ = 0;
    QString          live_prompt_;
    QString          live_status_;
};

} // namespace polymath
