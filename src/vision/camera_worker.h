#pragma once
//
// CameraWorker — one per enabled camera, running on its own QThread.  Owns an
// OpenCV VideoCapture on the camera's MJPEG/RTSP URL and runs the full pipeline:
//
//   grab -> (decimate) -> emit JPEG Frame on EventBus::frameReady
//        -> MOG2 motion gate
//        -> [on motion] YOLO person -> publish Detection + write events row
//        -> [on person + FaceRecognition on] SCRFD+ArcFace -> identity match
//
// Auto-reconnects with backoff when the stream drops.  The heavy ONNX stages
// (YoloDetector, FaceRecognizer) and the shared VisualMemory are injected so the
// service can share/configure them; the worker never touches another service
// directly — results go out on the EventBus.
//
#include "types.h"

#include <QObject>

#include <atomic>
#include <memory>
#include <string>

namespace cv { class VideoCapture; class Mat; }

namespace polymath {

class Database;
class MotionDetector;
class YoloDetector;
class FaceRecognizer;
class VisualMemory;

// Stages the worker should run, toggled live by the service from Config/privacy.
struct PipelineToggles {
    std::atomic<bool> face_recognition{true};
};

class CameraWorker : public QObject {
    Q_OBJECT
public:
    CameraWorker(int camera_id, std::string name, std::string url,
                 Database& db,
                 YoloDetector* yolo,
                 FaceRecognizer* faces,
                 VisualMemory& mem,
                 PipelineToggles& toggles,
                 QObject* parent = nullptr);
    ~CameraWorker() override;

    int  cameraId() const { return camera_id_; }
    bool online() const   { return online_.load(); }

public slots:
    void run();              // blocking capture loop; invoke via the worker thread
    void stop();             // request loop exit (thread-safe)
    void requestSnapshot();  // force-emit the next decoded frame as a Frame

signals:
    void onlineChanged(int camera_id, bool online);
    void finished();         // emitted when run() returns (for thread teardown)

private:
    bool openCapture(cv::VideoCapture& cap);
    void processFrame(const cv::Mat& bgr);
    Bytes encodeJpeg(const cv::Mat& bgr, int quality = 80) const;
    void  writeThumbAndEvent(const cv::Mat& bgr, const std::string& kind,
                             std::optional<int64_t> user_id, const std::string& label);

    int          camera_id_;
    std::string  name_;
    std::string  url_;
    Database&    db_;
    YoloDetector* yolo_;
    FaceRecognizer* faces_;
    VisualMemory& mem_;
    PipelineToggles& toggles_;

    std::unique_ptr<MotionDetector> motion_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> online_{false};
    std::atomic<bool> snapshot_pending_{false};

    int64_t frame_index_ = 0;
    int64_t last_person_event_ms_ = 0;   // debounce events to ~1/sec per camera

    void setOnline(bool on);

    static constexpr int kEmitEveryN   = 3;   // decimate UI frames (~1/3 of stream)
    static constexpr int kMotionEveryN = 2;   // run the motion gate every other frame
};

} // namespace polymath
