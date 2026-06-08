#pragma once
//
// CameraImageProvider — serves the latest live frame for each camera to QML via
// the "image://cameras/<id>" URL scheme.  CamerasView renders a tile per camera
// (Image { source: "image://cameras/<id>" }) and forces a reload on a timer / on
// the model's frameTick.
//
// THREADING: requestImage() is invoked by Qt's QML image pipeline (GUI thread),
// while updateFrame() is fed from EventBus::frameReady (vision worker thread).
// The latest encoded JPEG per camera id is stashed under a mutex; decoding to a
// QImage happens lazily inside requestImage(), so frames the UI never paints
// cost nothing beyond a buffer copy.
//
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

class QByteArray;

namespace polymath {

class CameraImageProvider : public QQuickImageProvider {
public:
    CameraImageProvider();

    // id is the camera id (the path after "image://cameras/").  A trailing
    // "?tick=N" cache-buster is tolerated and ignored.  Returns a placeholder
    // tile when no frame has arrived for that camera yet.
    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;

    // Store the newest JPEG for a camera id (thread-safe).  Cheap: copies bytes
    // only; the JPEG is decoded on demand in requestImage().
    void updateFrame(int cameraId, const QByteArray& jpeg);

    // Forget cached frames (e.g. when cameras are disabled in Privacy).
    void clear();
    void clear(int cameraId);

private:
    QImage placeholder(const QSize& requestedSize) const;

    mutable QMutex            mtx_;
    QHash<int, QByteArray>    latest_;   // cameraId -> encoded JPEG
};

} // namespace polymath
