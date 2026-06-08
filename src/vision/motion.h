#pragma once
//
// MotionDetector — cheap MOG2 background-subtraction gate.  Each camera worker
// owns one.  It answers "did something move in this frame, and where?" so the
// expensive ONNX stages (YOLO person, SCRFD/ArcFace face) only run on frames
// that actually changed.  Pure OpenCV; no GPU, no allocations on the hot path
// beyond what cv::createBackgroundSubtractorMOG2 needs internally.
//
#include <opencv2/core.hpp>
#include <opencv2/video/background_segm.hpp>

namespace polymath {

struct MotionResult {
    bool      moved = false;      // true once foreground area exceeds the threshold
    double    area_ratio = 0.0;   // fraction of the frame that is moving [0,1]
    cv::Rect  bbox;               // union bbox of motion contours (image coords)
};

class MotionDetector {
public:
    // min_area_ratio: fraction of the frame that must change to count as motion.
    // history / var_threshold map directly onto MOG2's constructor knobs.
    explicit MotionDetector(double min_area_ratio = 0.0025,
                            int history = 500,
                            double var_threshold = 16.0);

    // Feed a BGR frame (any size); returns whether motion was detected.  The
    // first `warmup_frames` calls only train the model and always report no
    // motion to avoid a burst of false positives on stream start.
    MotionResult update(const cv::Mat& bgr);

    void reset();   // drop the learned background (e.g. after a reconnect)

private:
    cv::Ptr<cv::BackgroundSubtractorMOG2> mog2_;
    cv::Mat   fg_;          // reusable foreground-mask scratch
    double    min_area_ratio_;
    int       warmup_left_;
    static constexpr int kWarmupFrames = 15;
};

} // namespace polymath
