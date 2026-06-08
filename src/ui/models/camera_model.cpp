#include "camera_model.h"

#include "database.h"

#include <QPair>

namespace polymath {

CameraModel::CameraModel(Database& db, QObject* parent)
    : QAbstractListModel(parent), db_(db) {}

int CameraModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(cameras_.size());
}

QVariant CameraModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= cameras_.size())
        return {};
    const Camera& c = cameras_.at(index.row());
    switch (role) {
        case IdRole:         return c.id;
        case NameRole:       return c.name;
        case UrlRole:        return c.url;
        case LocationRole:   return c.location;
        case EnabledRole:    return c.enabled;
        case LiveRole:       return c.live;
        case FrameTickRole:  return static_cast<qulonglong>(c.tick);
        default:             return {};
    }
}

QHash<int, QByteArray> CameraModel::roleNames() const {
    return {
        {IdRole,        "cameraId"},
        {NameRole,      "name"},
        {UrlRole,       "url"},
        {LocationRole,  "location"},
        {EnabledRole,   "enabled"},
        {LiveRole,      "live"},
        {FrameTickRole, "frameTick"},
    };
}

void CameraModel::refresh() {
    beginResetModel();
    // Preserve live/tick state across a metadata refresh so a reload of the
    // camera list does not blank out tiles that are actively streaming.
    QHash<int, QPair<bool, quint64>> prevLive;
    for (const Camera& c : cameras_) prevLive.insert(c.id, {c.live, c.tick});

    cameras_.clear();
    db_.query(
        "SELECT id,name,url,location,enabled FROM cameras ORDER BY id ASC",
        {},
        [&](const Row& r) {
            Camera c;
            c.id       = static_cast<int>(r.i64(0));
            c.name     = QString::fromStdString(r.text(1));
            c.url      = QString::fromStdString(r.text(2));
            c.location = QString::fromStdString(r.text(3));
            c.enabled  = r.i64(4) != 0;
            if (auto it = prevLive.constFind(c.id); it != prevLive.constEnd()) {
                c.live = it->first;
                c.tick = it->second;
            }
            cameras_.push_back(std::move(c));
        });
    endResetModel();
}

int CameraModel::rowForCameraId(int camera_id) const {
    for (int i = 0; i < cameras_.size(); ++i)
        if (cameras_.at(i).id == camera_id) return i;
    return -1;
}

void CameraModel::onFrame(const Frame& f) {
    const int row = rowForCameraId(f.camera_id);
    if (row < 0) return;   // a frame for a camera not in our (stale) list

    Camera& c = cameras_[row];
    const bool was_live = c.live;
    c.live = true;
    ++c.tick;

    const QModelIndex idx = index(row);
    QVector<int> changed{FrameTickRole};
    if (!was_live) changed.push_back(LiveRole);
    emit dataChanged(idx, idx, changed);
}

} // namespace polymath
