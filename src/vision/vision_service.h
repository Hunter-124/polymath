#pragma once
//
// VisionService — one worker per camera. Per stream:
//   decode (OpenCV) -> motion (MOG2) -> person (YOLO/ONNX)
//   -> face detect+embed (SCRFD+ArcFace/ONNX, privacy-gated) -> identity match
// Maintains a rolling "visual memory" so "find my keys" can answer last-seen.
// Publishes frames (UI tiles) and Detection/FindObject events on the EventBus.
//
#include "service.h"
#include <QObject>
#include <memory>
#include <vector>

namespace polymath {

class Database;
class InferenceManager;   // VLM used for open-vocabulary object finding

struct CameraConfig { int id; std::string name; std::string url; bool enabled; };

class VisionService : public QObject, public IService {
    Q_OBJECT
public:
    VisionService(Database& db, InferenceManager& inf, QObject* parent = nullptr);
    ~VisionService() override;

    void start() override;
    void stop() override;
    const char* serviceName() const override { return "vision"; }

    void reloadCameras();

public slots:
    void setCamerasEnabled(bool on);
    void setFaceRecognition(bool on);
    void enrollUser(qint64 user_id, const QString& name);  // capture face gallery
    void findObject(const QString& query);                 // -> findObjectDone
    void snapshot(int camera_id);
    // Camera vision Q&A: answer a free-form question about a camera's current view
    // (camera_id<0 = most recently active) using the VLM on the latest buffered
    // frame; replies on EventBus::cameraAnswer correlated by request_id.
    void describeCameraView(const QString& request_id, const QString& question, int camera_id);

signals:
    void cameraStateChanged(int camera_id, bool online);

private:
    // Worker lifecycle + gallery helpers (implementation detail; added in Wave-1).
    void startWorkers();
    void stopWorkers();
    void loadGalleryFromDb();

    struct Impl;
    std::unique_ptr<Impl> d_;
    Database&         db_;
    InferenceManager& inf_;
};

} // namespace polymath
