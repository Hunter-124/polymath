#include "camera_image_provider.h"

#include <QByteArray>
#include <QPainter>

namespace polymath {

CameraImageProvider::CameraImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

void CameraImageProvider::updateFrame(int cameraId, const QByteArray& jpeg) {
    if (jpeg.isEmpty()) return;
    QMutexLocker lk(&mtx_);
    latest_.insert(cameraId, jpeg);
}

void CameraImageProvider::clear() {
    QMutexLocker lk(&mtx_);
    latest_.clear();
}

void CameraImageProvider::clear(int cameraId) {
    QMutexLocker lk(&mtx_);
    latest_.remove(cameraId);
}

QImage CameraImageProvider::placeholder(const QSize& requestedSize) const {
    const int w = requestedSize.width()  > 0 ? requestedSize.width()  : 320;
    const int h = requestedSize.height() > 0 ? requestedSize.height() : 240;
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(QColor("#171a21"));
    QPainter p(&img);
    p.setPen(QColor("#565f89"));
    p.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("no signal"));
    return img;
}

QImage CameraImageProvider::requestImage(const QString& id, QSize* size,
                                         const QSize& requestedSize) {
    // Strip an optional cache-busting suffix ("12?tick=42" or "12&t=42").
    QString key = id;
    const int cut = key.indexOf(QLatin1Char('?'));
    if (cut >= 0) key.truncate(cut);

    bool ok = false;
    const int cameraId = key.toInt(&ok);

    QByteArray jpeg;
    if (ok) {
        QMutexLocker lk(&mtx_);
        auto it = latest_.constFind(cameraId);
        if (it != latest_.constEnd()) jpeg = it.value();
    }

    QImage img;
    if (!jpeg.isEmpty())
        img.loadFromData(reinterpret_cast<const uchar*>(jpeg.constData()),
                         jpeg.size(), "JPG");
    if (img.isNull())
        img = placeholder(requestedSize);

    if (requestedSize.isValid() && !requestedSize.isEmpty() &&
        requestedSize != img.size())
        img = img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (size) *size = img.size();
    return img;
}

} // namespace polymath
