#include "timeline_model.h"

#include "database.h"
#include "types.h"

#include <algorithm>
#include <ctime>

namespace polymath {

namespace {

// Build a "%term%" LIKE pattern, escaping the SQLite LIKE wildcards so a user
// typing '%' or '_' searches literally.  We pair this with ESCAPE '\' in SQL.
std::string likePattern(const QString& term) {
    QString esc;
    esc.reserve(term.size() + 4);
    for (QChar ch : term) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('%') || ch == QLatin1Char('_'))
            esc.append(QLatin1Char('\\'));
        esc.append(ch);
    }
    return ('%' + esc + '%').toStdString();
}

} // namespace

TimelineModel::TimelineModel(Database& db, QObject* parent)
    : QAbstractListModel(parent), db_(db) {}

int TimelineModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(entries_.size());
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size())
        return {};
    const Entry& e = entries_.at(index.row());
    switch (role) {
        case CategoryRole:  return e.category;
        case KindRole:      return e.kind;
        case TextRole:      return e.text;
        case TimestampRole: return static_cast<qlonglong>(e.ts);
        case TimeLabelRole: return timeLabel(e.ts);
        default:            return {};
    }
}

QHash<int, QByteArray> TimelineModel::roleNames() const {
    return {
        {CategoryRole,  "category"},
        {KindRole,      "kind"},
        {TextRole,      "text"},
        {TimestampRole, "timestamp"},
        {TimeLabelRole, "timeLabel"},
    };
}

QString TimelineModel::timeLabel(int64_t ts) {
    std::time_t tt = static_cast<std::time_t>(ts);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                  lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
    return QString::fromLatin1(buf);
}

void TimelineModel::refresh() {
    beginResetModel();
    entries_.clear();

    const bool filtered = !filter_.isEmpty();
    const std::string like = filtered ? likePattern(filter_) : std::string();
    const auto wantCategory = [this](QLatin1String c) {
        return category_filter_.isEmpty() || category_filter_ == c;
    };

    // --- events ---
    if (wantCategory(QLatin1String("event"))) {
        std::string sql =
            "SELECT ts,kind,label FROM events ";
        if (filtered) sql += "WHERE (kind LIKE ?1 ESCAPE '\\' OR label LIKE ?1 ESCAPE '\\') ";
        sql += "ORDER BY ts DESC LIMIT ?2";
        std::vector<nlohmann::json> params;
        if (filtered) params.push_back(like);
        params.push_back(static_cast<int64_t>(kLimit));
        db_.query(sql, params, [&](const Row& r) {
            Entry e;
            e.category = QStringLiteral("event");
            e.kind     = QString::fromStdString(r.text(1));
            const QString label = QString::fromStdString(r.text(2));
            e.text = label.isEmpty() ? e.kind : (e.kind + ": " + label);
            e.ts = r.i64(0);
            entries_.push_back(std::move(e));
        });
    }

    // --- transcripts ---
    if (wantCategory(QLatin1String("transcript"))) {
        std::string sql =
            "SELECT ts,is_ambient,text FROM transcripts ";
        if (filtered) sql += "WHERE text LIKE ?1 ESCAPE '\\' ";
        sql += "ORDER BY ts DESC LIMIT ?2";
        std::vector<nlohmann::json> params;
        if (filtered) params.push_back(like);
        params.push_back(static_cast<int64_t>(kLimit));
        db_.query(sql, params, [&](const Row& r) {
            Entry e;
            e.category = QStringLiteral("transcript");
            e.kind     = r.i64(1) != 0 ? QStringLiteral("ambient") : QStringLiteral("command");
            e.text     = QString::fromStdString(r.text(2));
            e.ts       = r.i64(0);
            entries_.push_back(std::move(e));
        });
    }

    // --- memories ---
    if (wantCategory(QLatin1String("memory"))) {
        std::string sql =
            "SELECT ts,kind,text FROM memories ";
        if (filtered) sql += "WHERE text LIKE ?1 ESCAPE '\\' ";
        sql += "ORDER BY ts DESC LIMIT ?2";
        std::vector<nlohmann::json> params;
        if (filtered) params.push_back(like);
        params.push_back(static_cast<int64_t>(kLimit));
        db_.query(sql, params, [&](const Row& r) {
            Entry e;
            e.category = QStringLiteral("memory");
            e.kind     = QString::fromStdString(r.text(1));
            e.text     = QString::fromStdString(r.text(2));
            e.ts       = r.i64(0);
            entries_.push_back(std::move(e));
        });
    }

    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const Entry& a, const Entry& b) { return a.ts > b.ts; });
    if (entries_.size() > kLimit) entries_.resize(kLimit);

    endResetModel();
}

void TimelineModel::setFilter(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed == filter_) return;
    filter_ = trimmed;
    refresh();
    emit filterChanged();
}

void TimelineModel::setCategoryFilter(const QString& category) {
    if (category == category_filter_) return;
    category_filter_ = category;
    refresh();
    emit categoryFilterChanged();
}

bool TimelineModel::matchesFilter(const Entry& e) const {
    if (!category_filter_.isEmpty() && e.category != category_filter_) return false;
    if (filter_.isEmpty()) return true;
    return e.text.contains(filter_, Qt::CaseInsensitive) ||
           e.kind.contains(filter_, Qt::CaseInsensitive);
}

void TimelineModel::prepend(Entry e) {
    if (!matchesFilter(e)) return;
    beginInsertRows({}, 0, 0);
    entries_.insert(0, std::move(e));
    endInsertRows();
    if (entries_.size() > kLimit) {
        const int last = static_cast<int>(entries_.size()) - 1;
        beginRemoveRows({}, last, last);
        entries_.removeAt(last);
        endRemoveRows();
    }
}

void TimelineModel::onDetection(const Detection& d) {
    Entry e;
    e.category = QStringLiteral("event");
    e.ts = to_unix(d.ts);

    // Summarize the detection's most salient label for the feed line.
    QString label;
    if (!d.boxes.empty()) label = QString::fromStdString(d.boxes.front().label);
    e.kind = label.isEmpty() ? QStringLiteral("motion") : label;

    QString line = QStringLiteral("camera %1").arg(d.camera_id);
    if (!label.isEmpty()) line = label + " on " + line;
    if (d.user_id.has_value())
        line += QStringLiteral(" (user %1)").arg(static_cast<qlonglong>(*d.user_id));
    e.text = line;
    prepend(std::move(e));
}

void TimelineModel::onUtterance(const Utterance& u) {
    if (u.text.empty()) return;
    Entry e;
    e.category = QStringLiteral("transcript");
    e.kind     = u.is_ambient ? QStringLiteral("ambient") : QStringLiteral("command");
    e.text     = QString::fromStdString(u.text);
    e.ts       = to_unix(u.ts);
    prepend(std::move(e));
}

} // namespace polymath
