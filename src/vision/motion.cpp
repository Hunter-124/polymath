#include "motion.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <vector>

namespace polymath {

MotionDetector::MotionDetector(double min_area_ratio, int history, double var_threshold)
    : min_area_ratio_(min_area_ratio), warmup_left_(kWarmupFrames) {
    // detectShadows=false: we only care about gross motion, and shadow pixels
    // (value 127 in the mask) would otherwise inflate the moving area.
    mog2_ = cv::createBackgroundSubtractorMOG2(history, var_threshold, /*detectShadows=*/false);
}

void MotionDetector::reset() {
    mog2_->clear();
    warmup_left_ = kWarmupFrames;
}

MotionResult MotionDetector::update(const cv::Mat& bgr) {
    MotionResult out;
    if (bgr.empty())
        return out;

    // MOG2 is happiest on a modestly sized, blurred input: downscaling keeps the
    // gate cheap and the blur suppresses sensor noise / compression speckle.
    cv::Mat small;
    const double scale = bgr.cols > 640 ? 640.0 / bgr.cols : 1.0;
    if (scale < 1.0)
        cv::resize(bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);
    else
        small = bgr;

    cv::Mat blurred;
    cv::GaussianBlur(small, blurred, cv::Size(5, 5), 0);

    // learningRate -1 lets MOG2 auto-tune from its history window.
    mog2_->apply(blurred, fg_, /*learningRate=*/-1.0);

    if (warmup_left_ > 0) {
        --warmup_left_;
        return out;   // still learning the background; suppress detections
    }

    // Threshold + morphological close to glue fragmented blobs together.
    cv::threshold(fg_, fg_, 200, 255, cv::THRESH_BINARY);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7));
    cv::morphologyEx(fg_, fg_, cv::MORPH_OPEN, kernel);
    cv::dilate(fg_, fg_, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(fg_, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    const double frame_area = static_cast<double>(small.cols) * small.rows;
    double moving_area = 0.0;
    cv::Rect uni;
    bool first = true;
    for (const auto& c : contours) {
        const double a = cv::contourArea(c);
        if (a < frame_area * 0.0005)   // ignore tiny specks
            continue;
        moving_area += a;
        cv::Rect r = cv::boundingRect(c);
        uni = first ? r : (uni | r);
        first = false;
    }

    out.area_ratio = frame_area > 0 ? moving_area / frame_area : 0.0;
    out.moved = out.area_ratio >= min_area_ratio_;

    if (out.moved && !first) {
        // Map the union bbox back to the original frame resolution.
        const double inv = 1.0 / scale;
        out.bbox = cv::Rect(
            cv::Point(static_cast<int>(uni.x * inv), static_cast<int>(uni.y * inv)),
            cv::Size(static_cast<int>(uni.width * inv), static_cast<int>(uni.height * inv)));
        out.bbox &= cv::Rect(0, 0, bgr.cols, bgr.rows);
    }
    return out;
}

} // namespace polymath
