#include "vision_service.h"
#include "database.h"
#include "event_bus.h"
#include "config.h"
#include "logging.h"

// Wave-0 compiling stub. Wave-1/2 vision agents implement Impl with per-camera
// workers (camera_worker.cpp), motion (motion.cpp), YOLO (detector_yolo.cpp),
// face recognition (face_arcface.cpp), and the visual-memory finder
// (visual_memory.cpp / finder.cpp using the InferenceManager VLM).

namespace polymath {

struct VisionService::Impl {
    bool cameras_enabled = true;
    bool face_recognition = true;
    std::vector<CameraConfig> cameras;
};

VisionService::VisionService(Database& db, InferenceManager& inf, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()), db_(db), inf_(inf) {}
VisionService::~VisionService() = default;

void VisionService::start() {
    Config cfg(db_);
    d_->cameras_enabled  = cfg.getBool(keys::CamerasEnabled);
    d_->face_recognition = cfg.getBool(keys::FaceRecognition);
    reloadCameras();
    PM_INFO("VisionService started (stub): {} cameras", d_->cameras.size());
}
void VisionService::stop() {}

void VisionService::reloadCameras() {
    d_->cameras.clear();
    db_.query("SELECT id,name,url,enabled FROM cameras", {}, [&](const Row& r) {
        d_->cameras.push_back({(int)r.i64(0), r.text(1), r.text(2), r.i64(3) != 0});
    });
}
void VisionService::setCamerasEnabled(bool on)   { d_->cameras_enabled = on; }
void VisionService::setFaceRecognition(bool on)  { d_->face_recognition = on; }
void VisionService::enrollUser(qint64, const QString&) {}
void VisionService::findObject(const QString& q) {
    EventBus::instance().publishFindObject(
        {q, QStringLiteral("[vision not yet implemented]"), -1, 0});
}
void VisionService::snapshot(int) {}

} // namespace polymath
