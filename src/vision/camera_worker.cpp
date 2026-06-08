#include "camera_worker.h"

#include "motion.h"
#include "detector_yolo.h"
#include "face_arcface.h"
#include "visual_memory.h"

#include "database.h"
#include "event_bus.h"
#include "paths.h"
#include "logging.h"

#include <opencv2/videoio.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QThread>

#include <chrono>
#include <cstdio>
#include <filesystem>

namespace polymath {

namespace {
int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now().time_since_epoch()).count();
}
} // namespace

CameraWorker::CameraWorker(int camera_id, std::string name, std::string url,
                           Database& db, YoloDetector* yolo, FaceRecognizer* faces,
                           VisualMemory& mem, PipelineToggles& toggles, QObject* parent)
    : QObject(parent), camera_id_(camera_id), name_(std::move(name)), url_(std::move(url)),
      db_(db), yolo_(yolo), faces_(faces), mem_(mem), toggles_(toggles),
      motion_(std::make_unique<MotionDetector>()) {}

CameraWorker::~CameraWorker() = default;

void CameraWorker::stop() { stop_.store(true); }

void CameraWorker::requestSnapshot() { snapshot_pending_.store(true); }

void CameraWorker::setOnline(bool on) {
    bool prev = online_.exchange(on);
    if (prev != on) {
        emit onlineChanged(camera_id_, on);
        EventBus::instance().publishNotice(
            {on ? "info" : "warn", "vision",
             "camera '" + name_ + "' " + (on ? "online" : "offline")});
    }
}

bool CameraWorker::openCapture(cv::VideoCapture& cap) {
    // Prefer FFMPEG for rtsp/http; OpenCV picks a sane backend from the URL.
    // For RTSP, favor TCP transport for resilience over lossy networks.
    if (url_.rfind("rtsp", 0) == 0) {
#ifdef _WIN32
        _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp");
#else
        setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp", 1);
#endif
    }
    cap.open(url_, cv::CAP_FFMPEG);
    if (!cap.isOpened())
        cap.open(url_);   // last resort: any available backend
    if (cap.isOpened()) {
        // Keep latency low: small internal buffer so we read near-live frames.
        cap.set(cv::CAP_PROP_BUFFERSIZE, 2);
    }
    return cap.isOpened();
}

Bytes CameraWorker::encodeJpeg(const cv::Mat& bgr, int quality) const {
    std::vector<uchar> buf;
    const std::vector<int> params{cv::IMWRITE_JPEG_QUALITY, quality};
    if (!cv::imencode(".jpg", bgr, buf, params))
        return {};
    return Bytes(buf.begin(), buf.end());
}

void CameraWorker::writeThumbAndEvent(const cv::Mat& bgr, const std::string& kind,
                                      std::optional<int64_t> user_id, const std::string& label) {
    std::string thumb_path;
    try {
        const auto dir = Paths::instance().media() / "events";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        char fname[128];
        std::snprintf(fname, sizeof fname, "cam%d_%lld_%s.jpg",
                      camera_id_, static_cast<long long>(now_ms()), kind.c_str());
        const auto full = dir / fname;
        // Downscale thumbnails to keep media/ small.
        cv::Mat thumb;
        const double s = bgr.cols > 480 ? 480.0 / bgr.cols : 1.0;
        if (s < 1.0) cv::resize(bgr, thumb, cv::Size(), s, s, cv::INTER_AREA);
        else         thumb = bgr;
        if (cv::imwrite(full.string(), thumb))
            thumb_path = full.string();
    } catch (const std::exception& e) {
        PM_WARN("CameraWorker: thumb write failed: {}", e.what());
    }

    db_.exec("INSERT INTO events(kind,camera_id,user_id,label,thumb_path,ts) "
             "VALUES(?1,?2,?3,?4,?5,?6)",
             {kind, camera_id_,
              user_id ? nlohmann::json(*user_id) : nlohmann::json(nullptr),
              label, thumb_path, to_unix(Clock::now())});
}

void CameraWorker::processFrame(const cv::Mat& bgr) {
    ++frame_index_;
    const auto ts = Clock::now();

    const bool force_emit = snapshot_pending_.exchange(false);

    // 1) Decimated UI frame on the bus (always publish snapshots immediately).
    if (force_emit || (frame_index_ % kEmitEveryN) == 0) {
        Frame f;
        f.camera_id = camera_id_;
        f.width = bgr.cols;
        f.height = bgr.rows;
        f.jpeg = encodeJpeg(bgr);
        f.ts = ts;
        if (!f.jpeg.empty()) {
            EventBus::instance().publishFrame(f);
            mem_.push(f);   // remember the frame (boxes added below if any run)
        }
    }

    // 2) Motion gate — cheap, every other frame.
    if ((frame_index_ % kMotionEveryN) != 0)
        return;
    const MotionResult motion = motion_->update(bgr);
    if (!motion.moved)
        return;

    // Record a lightweight motion event (debounced via the person path below).
    // 3) Person detection (only when YOLO is available).
    std::vector<BoundingBox> persons;
    if (yolo_ && yolo_->ready())
        persons = yolo_->detect(bgr);

    if (persons.empty()) {
        // Motion but no person: still useful for the activity log, debounced.
        if (now_ms() - last_person_event_ms_ > 2000) {
            last_person_event_ms_ = now_ms();
            writeThumbAndEvent(bgr, "motion", std::nullopt, "motion");
            Detection d;
            d.camera_id = camera_id_;
            d.ts = ts;
            // Express motion as a single coarse box (normalized).
            if (motion.bbox.area() > 0) {
                BoundingBox b;
                b.x = static_cast<float>(motion.bbox.x) / bgr.cols;
                b.y = static_cast<float>(motion.bbox.y) / bgr.rows;
                b.w = static_cast<float>(motion.bbox.width) / bgr.cols;
                b.h = static_cast<float>(motion.bbox.height) / bgr.rows;
                b.score = static_cast<float>(motion.area_ratio);
                b.label = "motion";
                d.boxes.push_back(b);
            }
            EventBus::instance().publishDetection(d);
        }
        return;
    }

    // 4) Face recognition on detected people (privacy-gated).
    std::optional<int64_t> resolved_user;
    std::string label = "person";
    if (toggles_.face_recognition.load() && faces_ && faces_->detectorReady()) {
        auto fboxes = faces_->detect(bgr);
        float best_sim = 0.f;
        for (const auto& fb : fboxes) {
            if (!faces_->embedderReady())
                break;
            Embedding emb = faces_->embed(bgr, fb);
            FaceMatch m = faces_->match(emb);
            if (m.user_id >= 0 && m.similarity > best_sim) {
                best_sim = m.similarity;
                resolved_user = m.user_id;
            }
        }
        if (resolved_user) {
            std::string uname;
            db_.query("SELECT name FROM users WHERE id=?1", {*resolved_user},
                      [&](const Row& r) { uname = r.text(0); });
            label = uname.empty() ? "known person" : uname;
        }
    }

    // 5) Publish the Detection and persist an events row (debounced).
    Detection d;
    d.camera_id = camera_id_;
    d.boxes = persons;
    d.user_id = resolved_user;
    d.ts = ts;
    EventBus::instance().publishDetection(d);

    // Keep the most recent buffered frame annotated with these boxes for Finder.
    {
        Frame f;
        f.camera_id = camera_id_;
        f.width = bgr.cols;
        f.height = bgr.rows;
        f.jpeg = encodeJpeg(bgr);
        f.ts = ts;
        if (!f.jpeg.empty())
            mem_.push(f, persons, resolved_user);
    }

    if (now_ms() - last_person_event_ms_ > 1000) {
        last_person_event_ms_ = now_ms();
        const std::string kind = resolved_user ? "face" : "person";
        writeThumbAndEvent(bgr, kind, resolved_user, label);
    }
}

void CameraWorker::run() {
    PM_INFO("CameraWorker[{}] '{}' starting on {}", camera_id_, name_, url_);
    int backoff_ms = 1000;
    const int backoff_max = 30000;

    while (!stop_.load()) {
        cv::VideoCapture cap;
        if (!openCapture(cap)) {
            setOnline(false);
            PM_WARN("CameraWorker[{}] open failed; retry in {} ms", camera_id_, backoff_ms);
            // Sleep in small slices so stop() is responsive during backoff.
            for (int slept = 0; slept < backoff_ms && !stop_.load(); slept += 100)
                QThread::msleep(100);
            backoff_ms = std::min(backoff_ms * 2, backoff_max);
            continue;
        }

        setOnline(true);
        backoff_ms = 1000;
        motion_->reset();
        int consecutive_fail = 0;

        cv::Mat frame;
        while (!stop_.load()) {
            if (!cap.read(frame) || frame.empty()) {
                if (++consecutive_fail > 30) {
                    PM_WARN("CameraWorker[{}] stream stalled; reconnecting", camera_id_);
                    break;   // drop out to reopen
                }
                QThread::msleep(20);
                continue;
            }
            consecutive_fail = 0;
            try {
                processFrame(frame);
            } catch (const std::exception& e) {
                PM_WARN("CameraWorker[{}] frame processing error: {}", camera_id_, e.what());
            }
        }
        cap.release();
        setOnline(false);
    }

    PM_INFO("CameraWorker[{}] stopped", camera_id_);
    emit finished();
}

} // namespace polymath
