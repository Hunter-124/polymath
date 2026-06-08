#include "vision_service.h"

#include "camera_worker.h"
#include "motion.h"
#include "detector_yolo.h"
#include "face_arcface.h"
#include "visual_memory.h"
#include "finder.h"

#include "database.h"
#include "event_bus.h"
#include "config.h"
#include "paths.h"
#include "logging.h"

#include <opencv2/videoio.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QThread>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <vector>

// Real Wave-1/2 vision pipeline. The service owns one CameraWorker per enabled
// camera (each on its own QThread), a shared YOLO person detector and SCRFD/
// ArcFace face recognizer (ONNX Runtime, CUDA EP), a rolling VisualMemory, and
// a VLM-backed Finder for "where did I last see X" queries.

namespace polymath {

namespace {

// Resolve a vision model path under <models>/vision/, falling back to <models>/.
std::string modelPath(const char* file) {
    const auto base = Paths::instance().models();
    auto p = base / "vision" / file;
    std::error_code ec;
    if (std::filesystem::exists(p, ec))
        return p.string();
    return (base / file).string();   // allow flat layout too
}

} // namespace

struct VisionService::Impl {
    explicit Impl(Database& db, InferenceManager& inf)
        : memory(), finder(inf, memory, db) {}

    bool cameras_enabled = true;
    PipelineToggles toggles;          // face_recognition lives here (atomic)
    std::vector<CameraConfig> cameras;

    // Shared heavy stages (thread-safe enough for our access pattern: workers
    // call detect()/embed() which only read immutable session state; the gallery
    // is swapped under a coarse rebuild, not mid-frame).
    std::unique_ptr<YoloDetector>   yolo;
    std::unique_ptr<FaceRecognizer> faces;

    VisualMemory memory;
    Finder       finder;

    // One running worker + its thread per active camera.
    struct Running {
        CameraWorker* worker = nullptr;   // owned via deleteLater on its thread
        QThread*      thread = nullptr;
    };
    std::vector<Running> running;
};

VisionService::VisionService(Database& db, InferenceManager& inf, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>(db, inf)), db_(db), inf_(inf) {}

VisionService::~VisionService() { stop(); }

void VisionService::start() {
    Config cfg(db_);
    d_->cameras_enabled = cfg.getBool(keys::CamerasEnabled);
    d_->toggles.face_recognition.store(cfg.getBool(keys::FaceRecognition));

    // Load the shared ONNX models once (best-effort; pipeline degrades if absent).
    d_->yolo = std::make_unique<YoloDetector>();
    if (!d_->yolo->load(modelPath("yolov8n.onnx"), /*use_cuda=*/true))
        PM_WARN("VisionService: YOLO model not loaded — person detection disabled");

    d_->faces = std::make_unique<FaceRecognizer>();
    if (!d_->faces->load(modelPath("scrfd_500m.onnx"), modelPath("arcface_r100.onnx"),
                         /*use_cuda=*/true))
        PM_WARN("VisionService: face models not loaded — face recognition disabled");
    loadGalleryFromDb();

    reloadCameras();
    PM_INFO("VisionService started: {} cameras, cameras_enabled={} face_rec={}",
            d_->cameras.size(), d_->cameras_enabled, d_->toggles.face_recognition.load());

    if (d_->cameras_enabled)
        startWorkers();
}

void VisionService::stop() {
    stopWorkers();
    d_->memory.clearAll();
    d_->yolo.reset();
    d_->faces.reset();
}

// --- camera registry --------------------------------------------------------

void VisionService::reloadCameras() {
    d_->cameras.clear();
    db_.query("SELECT id,name,url,enabled FROM cameras ORDER BY id", {}, [&](const Row& r) {
        d_->cameras.push_back({(int)r.i64(0), r.text(1), r.text(2), r.i64(3) != 0});
    });
}

void VisionService::startWorkers() {
    if (!d_->cameras_enabled)
        return;
    stopWorkers();   // idempotent: ensure a clean slate

    for (const auto& cam : d_->cameras) {
        if (!cam.enabled)
            continue;
        auto* worker = new CameraWorker(cam.id, cam.name, cam.url, db_,
                                        d_->yolo.get(), d_->faces.get(),
                                        d_->memory, d_->toggles);
        auto* thread = new QThread();
        worker->moveToThread(thread);

        connect(thread, &QThread::started, worker, &CameraWorker::run);
        connect(worker, &CameraWorker::finished, thread, &QThread::quit);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(worker, &CameraWorker::onlineChanged, this,
                &VisionService::cameraStateChanged);

        thread->start();
        d_->running.push_back({worker, thread});
        PM_INFO("VisionService: started worker for camera {} '{}'", cam.id, cam.name);
    }
}

void VisionService::stopWorkers() {
    for (auto& r : d_->running) {
        if (r.worker)
            r.worker->stop();   // thread-safe stop request
    }
    for (auto& r : d_->running) {
        if (r.thread) {
            r.thread->quit();
            if (!r.thread->wait(5000)) {
                PM_WARN("VisionService: worker thread did not exit cleanly; terminating");
                r.thread->terminate();
                r.thread->wait(1000);
            }
            r.thread->deleteLater();
        }
    }
    d_->running.clear();
}

// --- privacy / live toggles -------------------------------------------------

void VisionService::setCamerasEnabled(bool on) {
    if (d_->cameras_enabled == on)
        return;
    d_->cameras_enabled = on;
    PM_INFO("VisionService: cameras {}", on ? "enabled" : "disabled");
    if (on) {
        reloadCameras();
        startWorkers();
    } else {
        stopWorkers();
        d_->memory.clearAll();
    }
}

void VisionService::setFaceRecognition(bool on) {
    d_->toggles.face_recognition.store(on);   // workers read this live
    PM_INFO("VisionService: face recognition {}", on ? "on" : "off");
}

// --- enrollment -------------------------------------------------------------

void VisionService::loadGalleryFromDb() {
    if (!d_->faces)
        return;
    std::vector<FaceRecognizer::GalleryEntry> entries;
    db_.query("SELECT id,face_gallery FROM users WHERE face_gallery <> ''", {},
              [&](const Row& r) {
        const int64_t uid = r.i64(0);
        const std::string path = r.text(1);
        auto vecs = FaceRecognizer::loadGallery(path);
        for (auto& v : vecs)
            entries.push_back({uid, std::move(v)});
    });
    PM_INFO("VisionService: loaded face gallery ({} vectors)", entries.size());
    d_->faces->setGallery(std::move(entries));
}

void VisionService::enrollUser(qint64 user_id, const QString& name) {
    if (!d_->faces || !d_->faces->detectorReady() || !d_->faces->embedderReady()) {
        EventBus::instance().publishNotice(
            {"warn", "vision", "cannot enroll: face models not loaded"});
        return;
    }

    // Capture a handful of embeddings from whatever frames we can grab right now:
    // prefer the rolling memory; if empty, grab a few frames live per camera.
    std::vector<Embedding> embeddings;

    auto harvest = [&](const cv::Mat& bgr) {
        auto fboxes = d_->faces->detect(bgr);
        if (fboxes.empty())
            return;
        // Use the largest (closest) face in the frame.
        std::sort(fboxes.begin(), fboxes.end(), [](const FaceBox& a, const FaceBox& b) {
            return a.rect.area() > b.rect.area();
        });
        Embedding e = d_->faces->embed(bgr, fboxes.front());
        if (!e.empty())
            embeddings.push_back(std::move(e));
    };

    // Decode recent memory frames first.
    for (const auto& snap : d_->memory.recent(20)) {
        if (embeddings.size() >= 10)
            break;
        const auto& jpg = snap.frame.jpeg;
        cv::Mat m = cv::imdecode(cv::Mat(1, (int)jpg.size(), CV_8U, (void*)jpg.data()),
                                 cv::IMREAD_COLOR);
        if (!m.empty())
            harvest(m);
    }

    // If still thin, pull a few frames straight off each enabled camera.
    if (embeddings.size() < 3) {
        for (const auto& cam : d_->cameras) {
            if (!cam.enabled || embeddings.size() >= 10)
                continue;
            cv::VideoCapture cap;
            cap.open(cam.url, cv::CAP_FFMPEG);
            if (!cap.isOpened())
                cap.open(cam.url);
            if (!cap.isOpened())
                continue;
            cv::Mat m;
            for (int i = 0; i < 15 && embeddings.size() < 10; ++i) {
                if (cap.read(m) && !m.empty())
                    harvest(m);
            }
            cap.release();
        }
    }

    if (embeddings.empty()) {
        EventBus::instance().publishNotice(
            {"warn", "vision",
             QString::fromStdString("enrollment for '" + name.toStdString() + "' found no face")});
        return;
    }

    // Persist the gallery file under media/faces/<user_id>.gallery.
    std::string gallery_path;
    try {
        const auto dir = Paths::instance().media() / "faces";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        gallery_path = (dir / (std::to_string(user_id) + ".gallery")).string();
        FaceRecognizer::saveGallery(gallery_path, embeddings);
    } catch (const std::exception& e) {
        PM_ERROR("VisionService: failed to write gallery: {}", e.what());
        return;
    }

    // Upsert the users row (create if the caller passed an unseen id) and point
    // it at the gallery file.
    bool exists = false;
    db_.query("SELECT 1 FROM users WHERE id=?1", {(int64_t)user_id},
              [&](const Row&) { exists = true; });
    if (exists) {
        db_.exec("UPDATE users SET name=?2, face_gallery=?3 WHERE id=?1",
                 {(int64_t)user_id, name.toStdString(), gallery_path});
    } else {
        db_.exec("INSERT INTO users(id,name,face_gallery,created_at) VALUES(?1,?2,?3,?4)",
                 {(int64_t)user_id, name.toStdString(), gallery_path, to_unix(Clock::now())});
    }

    loadGalleryFromDb();   // refresh in-memory gallery for the workers
    PM_INFO("VisionService: enrolled user {} '{}' with {} embeddings",
            user_id, name.toStdString(), embeddings.size());
    EventBus::instance().publishNotice(
        {"info", "vision",
         QString::fromStdString("enrolled '" + name.toStdString() + "' (" +
             std::to_string(embeddings.size()) + " samples)")});
}

// --- object finder ----------------------------------------------------------

void VisionService::findObject(const QString& query) {
    // Runs on the vision worker thread; describeImage() blocks here, not the UI.
    FindObjectResult res = d_->finder.find(query.toStdString());
    EventBus::instance().publishFindObject(res);
}

// --- snapshot ---------------------------------------------------------------

void VisionService::snapshot(int camera_id) {
    // Ask the live worker to emit its next frame; if none is running, fall back
    // to the most recent buffered frame, or a one-shot grab.
    for (auto& r : d_->running) {
        if (r.worker && r.worker->cameraId() == camera_id) {
            QMetaObject::invokeMethod(r.worker, "requestSnapshot", Qt::QueuedConnection);
            return;
        }
    }

    Frame f = d_->memory.latest(camera_id);
    if (!f.jpeg.empty()) {
        EventBus::instance().publishFrame(f);
        return;
    }

    // One-shot grab for a disabled/idle camera.
    for (const auto& cam : d_->cameras) {
        if (cam.id != camera_id)
            continue;
        cv::VideoCapture cap;
        cap.open(cam.url, cv::CAP_FFMPEG);
        if (!cap.isOpened())
            cap.open(cam.url);
        cv::Mat m;
        if (cap.isOpened() && cap.read(m) && !m.empty()) {
            std::vector<uchar> buf;
            cv::imencode(".jpg", m, buf, {cv::IMWRITE_JPEG_QUALITY, 80});
            Frame out;
            out.camera_id = camera_id;
            out.width = m.cols;
            out.height = m.rows;
            out.jpeg = Bytes(buf.begin(), buf.end());
            out.ts = Clock::now();
            EventBus::instance().publishFrame(out);
        }
        cap.release();
        return;
    }
}

} // namespace polymath
