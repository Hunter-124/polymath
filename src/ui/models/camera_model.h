#pragma once
//
// CameraModel — a QAbstractListModel view over the `cameras` table.  Drives the
// tile grid in CamerasView.  Each tile renders its live frame through the
// CameraImageProvider ("image://cameras/<id>"); this model supplies the per-row
// id/name/location/enabled metadata plus a derived `live` flag that flips true
// when a frame for that camera has recently arrived on EventBus::frameReady.
//
#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

#include "types.h"

namespace polymath {

class Database;

class CameraModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        UrlRole,
        LocationRole,
        EnabledRole,
        LiveRole,         // a frame has arrived recently for this camera
        FrameTickRole     // monotonically bumped per frame; lets QML force Image reload
    };

    explicit CameraModel(Database& db, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

public slots:
    // Connected (queued) to EventBus::frameReady so the model learns which
    // cameras are live.  Runs on the UI thread; cheap (no pixel copy here — the
    // JPEG itself is cached by CameraImageProvider).
    void onFrame(const polymath::Frame& f);

private:
    struct Camera {
        int     id = -1;
        QString name;
        QString url;
        QString location;
        bool    enabled = true;
        bool    live = false;
        quint64 tick = 0;     // bumped each frame to invalidate the QML image cache
    };

    int rowForCameraId(int camera_id) const;

    Database&       db_;
    QVector<Camera> cameras_;
};

} // namespace polymath
