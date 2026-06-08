#pragma once
//
// TimelineModel — the unified Memory & Timeline feed (TimelineView).  It merges
// three SQLite tables into one reverse-chronological list:
//   * events      (motion|person|face|sound detections)
//   * transcripts (command + ambient speech)
//   * memories    (notes|facts|summaries|captions)
// Each row carries a `category` discriminator so the delegate can style it.  A
// free-text filter re-queries with a LIKE across the textual columns.  Live
// detections/utterances arriving on the EventBus are prepended in place.
//
#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

#include "event_bus.h"

namespace polymath {

class Database;

class TimelineModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
public:
    enum Roles {
        CategoryRole = Qt::UserRole + 1,   // "event" | "transcript" | "memory"
        KindRole,                          // table-specific subtype (motion, note, ...)
        TextRole,                          // human-readable line
        TimestampRole,                     // unix seconds
        TimeLabelRole                      // preformatted local "YYYY-MM-DD HH:MM"
    };

    explicit TimelineModel(Database& db, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Reload the merged feed.  An empty filter loads the most recent rows; a
    // non-empty filter applies a case-insensitive substring match.
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setFilter(const QString& text);
    QString filter() const { return filter_; }

public slots:
    // Queued from the EventBus (worker -> UI thread).
    void onDetection(const polymath::Detection& d);
    void onUtterance(const polymath::Utterance& u);

signals:
    void filterChanged();

private:
    struct Entry {
        QString category;
        QString kind;
        QString text;
        int64_t ts = 0;
    };

    void prepend(Entry e);                 // insert at row 0 if it passes the filter
    bool matchesFilter(const Entry& e) const;
    static QString timeLabel(int64_t ts);

    Database&      db_;
    QVector<Entry> entries_;
    QString        filter_;

    static constexpr int kLimit = 500;     // per-source cap before the merge
};

} // namespace polymath
