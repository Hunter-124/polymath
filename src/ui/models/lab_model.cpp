#include "lab_model.h"

#include "database.h"

namespace polymath {

LabModel::LabModel(Database& db, QObject* parent)
    : QAbstractListModel(parent), db_(db) {}

int LabModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(sessions_.size());
}

QVariant LabModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= sessions_.size())
        return {};
    const Session& s = sessions_.at(index.row());
    switch (role) {
        case SessionIdRole:     return static_cast<qlonglong>(s.id);
        case TitleRole:         return s.title;
        case ObjectiveRole:     return s.objective;
        case StatusRole:        return s.status;
        case StartedAtRole:     return static_cast<qlonglong>(s.started_at);
        case ReportDocIdRole:   return static_cast<qlonglong>(s.report_doc_id);
        case StepCountRole:     return s.step_count;
        case VerifiedCountRole: return s.verified_count;
        default:                return {};
    }
}

QHash<int, QByteArray> LabModel::roleNames() const {
    return {
        {SessionIdRole,     "sessionId"},
        {TitleRole,         "title"},
        {ObjectiveRole,     "objective"},
        {StatusRole,        "status"},
        {StartedAtRole,     "startedAt"},
        {ReportDocIdRole,   "reportDocId"},
        {StepCountRole,     "stepCount"},
        {VerifiedCountRole, "verifiedCount"},
    };
}

void LabModel::refresh() {
    beginResetModel();
    sessions_.clear();
    db_.query(
        "SELECT s.id, s.title, s.objective, s.status, s.started_at, "
        "  COALESCE(s.report_doc_id, 0), "
        "  (SELECT COUNT(*) FROM lab_session_steps st WHERE st.session_id=s.id), "
        "  (SELECT COUNT(*) FROM lab_session_steps st WHERE st.session_id=s.id AND st.verified=1) "
        "FROM lab_sessions s "
        "ORDER BY (s.status='active') DESC, s.started_at DESC LIMIT 200",
        {},
        [&](const Row& r) {
            Session s;
            s.id             = r.i64(0);
            s.title          = QString::fromStdString(r.text(1));
            s.objective      = QString::fromStdString(r.text(2));
            s.status         = QString::fromStdString(r.text(3));
            s.started_at     = r.i64(4);
            s.report_doc_id  = r.i64(5);
            s.step_count     = static_cast<int>(r.i64(6));
            s.verified_count = static_cast<int>(r.i64(7));
            sessions_.push_back(std::move(s));
        });
    endResetModel();
    emit countsChanged();
}

QVariantList LabModel::steps(qlonglong sessionId) const {
    QVariantList out;
    db_.query(
        "SELECT step_no, prompt, expected_kind, expected_unit, measured_value, "
        "  measured_unit, verified FROM lab_session_steps "
        "WHERE session_id=?1 ORDER BY step_no ASC",
        {static_cast<int64_t>(sessionId)},
        [&](const Row& r) {
            QVariantMap m;
            m["stepNo"]        = static_cast<int>(r.i64(0));
            m["prompt"]        = QString::fromStdString(r.text(1));
            m["expectedKind"]  = QString::fromStdString(r.text(2));
            m["expectedUnit"]  = QString::fromStdString(r.text(3));
            m["measuredValue"] = r.isNull(4) ? QVariant() : QVariant(r.dbl(4));
            m["measuredUnit"]  = QString::fromStdString(r.text(5));
            m["verified"]      = r.i64(6) != 0;
            out.push_back(m);
        });
    return out;
}

int LabModel::activeCount() const {
    int n = 0;
    for (const Session& s : sessions_) n += (s.status == QLatin1String("active")) ? 1 : 0;
    return n;
}

void LabModel::onLabStep(const LabStepEvent& e) {
    // Sessions are few; a full refresh keeps tallies authoritative without
    // bespoke row patching. Then surface the live step for the banner.
    refresh();
    live_session_ = e.session_id;
    live_prompt_  = e.prompt;
    live_status_  = e.status;
    emit liveStepChanged();
}

} // namespace polymath
