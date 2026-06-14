#include "instrument_model.h"

#include "database.h"

namespace polymath {

InstrumentModel::InstrumentModel(Database& db, QObject* parent)
    : QAbstractListModel(parent), db_(db) {}

int InstrumentModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(instruments_.size());
}

QVariant InstrumentModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= instruments_.size())
        return {};
    const Instrument& it = instruments_.at(index.row());
    switch (role) {
        case InstrumentIdRole: return it.id;
        case NameRole:         return it.name;
        case UnitRole:         return it.unit;
        case DeviceClassRole:  return it.device_class;
        case ValueRole:        return it.value;
        case InRangeRole:      return it.in_range;
        case HasReadingRole:   return it.has_reading;
        case TsRole:           return static_cast<qlonglong>(it.ts);
        default:               return {};
    }
}

QHash<int, QByteArray> InstrumentModel::roleNames() const {
    return {
        {InstrumentIdRole, "instrumentId"},
        {NameRole,         "name"},
        {UnitRole,         "unit"},
        {DeviceClassRole,  "deviceClass"},
        {ValueRole,        "value"},
        {InRangeRole,      "inRange"},
        {HasReadingRole,   "hasReading"},
        {TsRole,           "ts"},
    };
}

void InstrumentModel::refresh() {
    beginResetModel();
    instruments_.clear();
    // Each instrument + its latest reading (LEFT JOIN so unread instruments show).
    db_.query(
        "SELECT i.id, i.name, i.unit, i.device_class, "
        "  (SELECT m.value    FROM measurements m WHERE m.instrument_id=i.id ORDER BY m.ts DESC, m.id DESC LIMIT 1), "
        "  (SELECT m.in_range FROM measurements m WHERE m.instrument_id=i.id ORDER BY m.ts DESC, m.id DESC LIMIT 1), "
        "  (SELECT m.ts       FROM measurements m WHERE m.instrument_id=i.id ORDER BY m.ts DESC, m.id DESC LIMIT 1) "
        "FROM instruments i ORDER BY i.device_class, i.name",
        {},
        [&](const Row& r) {
            Instrument it;
            it.id           = QString::fromStdString(r.text(0));
            it.name         = QString::fromStdString(r.text(1));
            it.unit         = QString::fromStdString(r.text(2));
            it.device_class = QString::fromStdString(r.text(3));
            it.has_reading  = !r.isNull(4);
            it.value        = it.has_reading ? r.dbl(4) : 0.0;
            it.in_range     = r.isNull(5) ? true : (r.i64(5) != 0);
            it.ts           = r.isNull(6) ? 0 : r.i64(6);
            instruments_.push_back(std::move(it));
        });
    endResetModel();
    emit countsChanged();
}

int InstrumentModel::rowForId(const QString& id) const {
    for (int i = 0; i < instruments_.size(); ++i)
        if (instruments_.at(i).id == id) return i;
    return -1;
}

bool InstrumentModel::loadOne(const QString& id, Instrument& out) const {
    bool found = false;
    db_.query("SELECT id,name,unit,device_class FROM instruments WHERE id=?1",
              {id.toStdString()}, [&](const Row& r) {
                  out.id           = QString::fromStdString(r.text(0));
                  out.name         = QString::fromStdString(r.text(1));
                  out.unit         = QString::fromStdString(r.text(2));
                  out.device_class = QString::fromStdString(r.text(3));
                  found = true;
              });
    return found;
}

void InstrumentModel::onReading(const InstrumentReading& r) {
    const int row = rowForId(r.instrument_id);
    if (row >= 0) {
        Instrument& it = instruments_[row];
        it.value       = r.value;
        it.in_range    = r.in_range;
        it.has_reading = true;
        it.ts          = r.ts;
        if (it.unit.isEmpty() && !r.unit.isEmpty()) it.unit = r.unit;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {ValueRole, InRangeRole, HasReadingRole, TsRole, UnitRole});
        return;
    }
    // Unseen instrument: pull its registry row, fall back to the event payload.
    Instrument it;
    if (!loadOne(r.instrument_id, it)) {
        it.id           = r.instrument_id;
        it.name         = r.instrument_id;
        it.unit         = r.unit;
        it.device_class = r.device_class;
    }
    it.value = r.value; it.in_range = r.in_range; it.has_reading = true; it.ts = r.ts;
    beginInsertRows({}, instruments_.size(), instruments_.size());
    instruments_.push_back(std::move(it));
    endInsertRows();
    emit countsChanged();
}

} // namespace polymath
