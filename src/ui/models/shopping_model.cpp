#include "shopping_model.h"

#include "database.h"
#include "logging.h"
#include "types.h"

namespace polymath {

ShoppingModel::ShoppingModel(Database& db, QObject* parent)
    : QAbstractListModel(parent), db_(db) {}

int ShoppingModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(items_.size());
}

QVariant ShoppingModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= items_.size())
        return {};
    const Item& it = items_.at(index.row());
    switch (role) {
        case IdRole:        return static_cast<qlonglong>(it.id);
        case ItemRole:      return it.item;
        case QuantityRole:  return it.quantity;
        case DoneRole:      return it.done;
        case CreatedAtRole: return static_cast<qlonglong>(it.created_at);
        default:            return {};
    }
}

QHash<int, QByteArray> ShoppingModel::roleNames() const {
    return {
        {IdRole,        "itemId"},
        {ItemRole,      "item"},
        {QuantityRole,  "quantity"},
        {DoneRole,      "done"},
        {CreatedAtRole, "createdAt"},
    };
}

int ShoppingModel::remainingCount() const {
    int n = 0;
    for (const Item& it : items_) n += it.done ? 0 : 1;
    return n;
}

int ShoppingModel::doneCount() const {
    return static_cast<int>(items_.size()) - remainingCount();
}

void ShoppingModel::refresh() {
    beginResetModel();
    items_.clear();
    db_.query(
        "SELECT id,item,quantity,done,created_at FROM shopping_items "
        "ORDER BY done ASC, created_at DESC LIMIT 1000",
        {},
        [&](const Row& r) {
            Item it;
            it.id         = r.i64(0);
            it.item       = QString::fromStdString(r.text(1));
            it.quantity   = QString::fromStdString(r.text(2));
            it.done       = r.i64(3) != 0;
            it.created_at = r.i64(4);
            items_.push_back(std::move(it));
        });
    endResetModel();
    emit countsChanged();
}

void ShoppingModel::addItem(const QString& item, const QString& quantity) {
    const QString trimmed = item.trimmed();
    if (trimmed.isEmpty()) return;

    const int64_t now = to_unix(Clock::now());
    const int64_t id = db_.exec(
        "INSERT INTO shopping_items(item,quantity,done,created_at) VALUES(?1,?2,0,?3)",
        {trimmed.toStdString(), quantity.toStdString(), now});
    if (id < 0) {
        PM_WARN("ShoppingModel: insert failed for '{}'", trimmed.toStdString());
        return;
    }

    // New, not-done items sort to the top (created_at DESC within done=0).
    beginInsertRows({}, 0, 0);
    items_.insert(0, Item{id, trimmed, quantity, false, now});
    endInsertRows();
    emit countsChanged();
}

int ShoppingModel::rowForId(int64_t id) const {
    for (int i = 0; i < items_.size(); ++i)
        if (items_.at(i).id == id) return i;
    return -1;
}

void ShoppingModel::setDone(int row, bool done) {
    if (row < 0 || row >= items_.size()) return;
    Item& it = items_[row];
    if (it.done == done) return;

    db_.exec("UPDATE shopping_items SET done=?1 WHERE id=?2",
             {done ? 1 : 0, static_cast<int64_t>(it.id)});
    it.done = done;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {DoneRole});
    emit countsChanged();
}

void ShoppingModel::removeItem(int row) {
    if (row < 0 || row >= items_.size()) return;
    const int64_t id = items_.at(row).id;
    db_.exec("DELETE FROM shopping_items WHERE id=?1", {static_cast<int64_t>(id)});
    beginRemoveRows({}, row, row);
    items_.removeAt(row);
    endRemoveRows();
    emit countsChanged();
}

void ShoppingModel::clearDone() {
    db_.exec("DELETE FROM shopping_items WHERE done=1", {});
    // Reload rather than splice a (possibly non-contiguous) set of removals.
    refresh();
}

} // namespace polymath
