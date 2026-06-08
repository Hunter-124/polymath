#pragma once
//
// YoloDetector — YOLOv8/11n person detector on ONNX Runtime (CUDA EP, CPU
// fallback).  Pinned to ONNX Runtime 1.17.x (Ort::Session / Ort::Value C++ API).
//
// Model: an exported Ultralytics YOLOv8n/11n detection model at
//   <models>/vision/yolov8n.onnx  (dynamic-batch=1, input 1x3x640x640 RGB,
//   output 1x84x8400 — [cx,cy,w,h, 80 class scores] per anchor, no objectness).
// We only surface the COCO "person" class (index 0); other classes are dropped
// to keep Detection lightweight, but the score threshold / NMS are generic.
//
// All methods are synchronous and meant to run on a camera worker thread.
//
#include "types.h"

#include <opencv2/core.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Ort { struct Env; struct Session; struct MemoryInfo; }

namespace polymath {

class YoloDetector {
public:
    YoloDetector();
    ~YoloDetector();

    // Loads the .onnx model. use_cuda requests the CUDA execution provider and
    // silently falls back to CPU if it is unavailable. Returns false if the file
    // cannot be loaded at all (worker then skips the person stage).
    bool load(const std::string& model_path, bool use_cuda = true);
    bool ready() const { return session_ != nullptr; }

    // Runs detection on a BGR frame and returns person boxes in *image* pixel
    // coordinates (BoundingBox.x/y/w/h are normalized [0,1] to match the rest of
    // the vision contract). Empty vector if not ready or nothing found.
    std::vector<BoundingBox> detect(const cv::Mat& bgr);

    void setConfThreshold(float t) { conf_thresh_ = t; }
    void setIouThreshold(float t)  { iou_thresh_ = t; }

private:
    // Letterbox-resize into the 640x640 network input, recording scale/pad so
    // boxes can be mapped back to the source frame.
    struct LetterboxInfo { float scale; int pad_x; int pad_y; };
    LetterboxInfo preprocess(const cv::Mat& bgr, std::vector<float>& chw_out) const;

    std::unique_ptr<Ort::Env>        env_;
    std::unique_ptr<Ort::Session>    session_;
    std::unique_ptr<Ort::MemoryInfo> mem_info_;

    std::string  input_name_;
    std::string  output_name_;
    int          in_w_ = 640;
    int          in_h_ = 640;

    float        conf_thresh_ = 0.35f;
    float        iou_thresh_ = 0.45f;
    static constexpr int kPersonClass = 0;   // COCO "person"
};

} // namespace polymath
