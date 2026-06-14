#pragma once
//
// InstrumentModel — a QAbstractListModel over the `instruments` table joined with
// each instrument's latest `measurements` reading, for the desktop Lab cockpit's
// live readout.  Refreshes from SQLite and stays live by listening to
// EventBus::instrumentReading (the device fabric emits one per pushed reading);
// the matching row's value / in-range / ts update in place (new instruments are
// pulled from the DB on first sighting).
//
#include <QAbstractListModel>
#include <QString>
#include <QVector>

#include "event_bus.h"

namespace polymath {

class Database;

class InstrumentModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countsChanged)
public:
    enum Roles {
        InstrumentIdRole = Qt::UserRole + 1,
        NameRole,
        UnitRole,
        DeviceClassRole,
        ValueRole,
        InRangeRole,
        HasReadingRole,
        TsRole,
    };

    explicit InstrumentModel(Database& db, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

signals:
    void countsChanged();

public slots:
    // Queued connection from EventBus::instrumentReading (fabric -> UI thread).
    void onReading(const polymath::InstrumentReading& r);

private:
    struct Instrument {
        QString id;
        QString name;
        QString unit;
        QString device_class;
        double  value = 0.0;
        bool    in_range = true;
        bool    has_reading = false;
        int64_t ts = 0;
    };

    int  rowForId(const QString& id) const;
    bool loadOne(const QString& id, Instrument& out) const;

    Database&           db_;
    QVector<Instrument> instruments_;
};

} // namespace polymath
