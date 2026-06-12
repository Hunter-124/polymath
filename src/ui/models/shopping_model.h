#pragma once
//
// ShoppingModel — a QAbstractListModel view over the `shopping_items` table.
// The table is the source of truth (schema.h); this model is a cached mirror the
// ShoppingView binds to.  Mutations write through to SQLite (via the Database
// wrapper) and then patch the in-memory rows so the view updates immediately
// without a full reload.
//
#include <QAbstractListModel>
#include <QString>
#include <QVector>

namespace polymath {

class Database;

class ShoppingModel : public QAbstractListModel {
    Q_OBJECT
    // Live counts for headers/dashboard ("4 to buy · 2 bought").
    Q_PROPERTY(int remainingCount READ remainingCount NOTIFY countsChanged)
    Q_PROPERTY(int doneCount      READ doneCount      NOTIFY countsChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        ItemRole,
        QuantityRole,
        DoneRole,
        CreatedAtRole
    };

    explicit ShoppingModel(Database& db, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Reload all rows from SQLite (cheap: a single bounded, ordered SELECT).
    Q_INVOKABLE void refresh();

    // Insert a new item (writes the DB row, then appends locally).
    Q_INVOKABLE void addItem(const QString& item, const QString& quantity = {});

    // Toggle the done flag for the row at `index`.
    Q_INVOKABLE void setDone(int row, bool done);

    // Delete the row at `index`.
    Q_INVOKABLE void removeItem(int row);

    // Drop completed items (a common "clear bought" action).
    Q_INVOKABLE void clearDone();

    int remainingCount() const;
    int doneCount() const;

signals:
    void countsChanged();

private:
    struct Item {
        int64_t id = 0;
        QString item;
        QString quantity;
        bool    done = false;
        int64_t created_at = 0;
    };

    int rowForId(int64_t id) const;

    Database&     db_;
    QVector<Item> items_;
};

} // namespace polymath
