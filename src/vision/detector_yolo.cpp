#include "detector_yolo.h"

#include "logging.h"

#include <onnxruntime_cxx_api.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>   // cv::dnn::NMSBoxes

#include <algorithm>
#include <array>
#include <cmath>

namespace polymath {

YoloDetector::YoloDetector() = default;
YoloDetector::~YoloDetector() = default;

bool YoloDetector::load(const std::string& model_path, bool use_cuda) {
    try {
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "pm_yolo");

        Ort::SessionOptions opts;
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        opts.SetIntraOpNumThreads(1);

        if (use_cuda) {
            try {
                OrtCUDAProviderOptions cuda{};   // device_id 0, defaults are fine
                opts.AppendExecutionProvider_CUDA(cuda);
                PM_INFO("YoloDetector: CUDA execution provider enabled");
            } catch (const Ort::Exception& e) {
                PM_WARN("YoloDetector: CUDA EP unavailable ({}), falling back to CPU", e.what());
            }
        }

#ifdef _WIN32
        std::wstring wpath(model_path.begin(), model_path.end());
        session_ = std::make_unique<Ort::Session>(*env_, wpath.c_str(), opts);
#else
        session_ = std::make_unique<Ort::Session>(*env_, model_path.c_str(), opts);
#endif
        mem_info_ = std::make_unique<Ort::MemoryInfo>(
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

        Ort::AllocatorWithDefaultOptions alloc;
        input_name_  = session_->GetInputNameAllocated(0, alloc).get();
        output_name_ = session_->GetOutputNameAllocated(0, alloc).get();

        // Pick up a static input HxW if the model declares one (else keep 640).
        auto in_shape = session_->GetInputTypeInfo(0)
                            .GetTensorTypeAndShapeInfo().GetShape();
        if (in_shape.size() == 4) {
            if (in_shape[2] > 0) in_h_ = static_cast<int>(in_shape[2]);
            if (in_shape[3] > 0) in_w_ = static_cast<int>(in_shape[3]);
        }
        PM_INFO("YoloDetector loaded '{}' in={} out={} {}x{}",
                model_path, input_name_, output_name_, in_w_, in_h_);
        return true;
    } catch (const std::exception& e) {
        PM_ERROR("YoloDetector::load failed for '{}': {}", model_path, e.what());
        session_.reset();
        return false;
    }
}

YoloDetector::LetterboxInfo
YoloDetector::preprocess(const cv::Mat& bgr, std::vector<float>& chw_out) const {
    const float scale = std::min(static_cast<float>(in_w_) / bgr.cols,
                                 static_cast<float>(in_h_) / bgr.rows);
    const int new_w = static_cast<int>(std::round(bgr.cols * scale));
    const int new_h = static_cast<int>(std::round(bgr.rows * scale));
    const int pad_x = (in_w_ - new_w) / 2;
    const int pad_y = (in_h_ - new_h) / 2;

    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

    cv::Mat canvas(in_h_, in_w_, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(canvas(cv::Rect(pad_x, pad_y, new_w, new_h)));

    // BGR -> RGB, normalize to [0,1], and pack into CHW float.
    cv::Mat rgb;
    cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgb, CV_32FC3, 1.0 / 255.0);

    chw_out.resize(static_cast<size_t>(3) * in_h_ * in_w_);
    std::array<cv::Mat, 3> ch;
    for (int c = 0; c < 3; ++c)
        ch[c] = cv::Mat(in_h_, in_w_, CV_32FC1, chw_out.data() + static_cast<size_t>(c) * in_h_ * in_w_);
    cv::split(rgb, ch.data());

    return {scale, pad_x, pad_y};
}

std::vector<BoundingBox> YoloDetector::detect(const cv::Mat& bgr) {
    std::vector<BoundingBox> result;
    if (!session_ || bgr.empty())
        return result;

    try {
        std::vector<float> input;
        const LetterboxInfo lb = preprocess(bgr, input);

        const std::array<int64_t, 4> in_dims{1, 3, in_h_, in_w_};
        Ort::Value in_tensor = Ort::Value::CreateTensor<float>(
            *mem_info_, input.data(), input.size(), in_dims.data(), in_dims.size());

        const char* in_names[]  = {input_name_.c_str()};
        const char* out_names[] = {output_name_.c_str()};
        auto outputs = session_->Run(Ort::RunOptions{nullptr},
                                     in_names, &in_tensor, 1, out_names, 1);

        const float* data = outputs[0].GetTensorData<float>();
        auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        // Expected YOLOv8/11 detect output: [1, 4+num_classes, num_anchors].
        if (shape.size() != 3)
            return result;
        const int channels = static_cast<int>(shape[1]);   // 4 + num_classes
        const int anchors  = static_cast<int>(shape[2]);
        const int num_classes = channels - 4;
        if (num_classes <= kPersonClass)
            return result;

        // Output is channel-major: value(c, a) = data[c * anchors + a].
        auto at = [&](int c, int a) { return data[static_cast<size_t>(c) * anchors + a]; };

        std::vector<cv::Rect> nms_boxes;
        std::vector<float>    nms_scores;
        std::vector<BoundingBox> candidates;

        for (int a = 0; a < anchors; ++a) {
            const float score = at(4 + kPersonClass, a);
            if (score < conf_thresh_)
                continue;
            // Keep only anchors where "person" is the dominant class.
            bool dominant = true;
            for (int c = 0; c < num_classes; ++c) {
                if (c == kPersonClass) continue;
                if (at(4 + c, a) > score) { dominant = false; break; }
            }
            if (!dominant)
                continue;

            const float cx = at(0, a), cy = at(1, a), w = at(2, a), h = at(3, a);
            // Undo letterbox: network coords -> source pixels.
            const float x0 = (cx - w * 0.5f - lb.pad_x) / lb.scale;
            const float y0 = (cy - h * 0.5f - lb.pad_y) / lb.scale;
            const float bw = w / lb.scale;
            const float bh = h / lb.scale;

            nms_boxes.emplace_back(cv::Rect(static_cast<int>(x0), static_cast<int>(y0),
                                            static_cast<int>(bw), static_cast<int>(bh)));
            nms_scores.push_back(score);

            BoundingBox bb;
            bb.x = x0; bb.y = y0; bb.w = bw; bb.h = bh;   // pixels for now; normalized below
            bb.score = score;
            bb.label = "person";
            candidates.push_back(bb);
        }

        std::vector<int> keep;
        cv::dnn::NMSBoxes(nms_boxes, nms_scores, conf_thresh_, iou_thresh_, keep);

        const float fw = static_cast<float>(bgr.cols);
        const float fh = static_cast<float>(bgr.rows);
        for (int idx : keep) {
            BoundingBox bb = candidates[idx];
            // Clamp to frame then normalize to [0,1] per the Detection contract.
            float x0 = std::clamp(bb.x, 0.0f, fw);
            float y0 = std::clamp(bb.y, 0.0f, fh);
            float x1 = std::clamp(bb.x + bb.w, 0.0f, fw);
            float y1 = std::clamp(bb.y + bb.h, 0.0f, fh);
            bb.x = x0 / fw; bb.y = y0 / fh;
            bb.w = (x1 - x0) / fw; bb.h = (y1 - y0) / fh;
            result.push_back(bb);
        }
    } catch (const std::exception& e) {
        PM_WARN("YoloDetector::detect failed: {}", e.what());
    }
    return result;
}

} // namespace polymath
