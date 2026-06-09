#include "face_arcface.h"

#include "logging.h"

#include <onnxruntime_cxx_api.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>   // cv::estimateAffinePartial2D (face alignment)
#include <opencv2/dnn.hpp>       // cv::dnn::NMSBoxes

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>

namespace polymath {

namespace {

// Build / open a Session for a model file, sharing one Env. Returns nullptr on
// failure (the recognizer degrades gracefully when a model is missing).
std::unique_ptr<Ort::Session>
makeSession(Ort::Env& env, const std::string& path, bool use_cuda) {
    if (path.empty())
        return nullptr;
    try {
        Ort::SessionOptions opts;
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        opts.SetIntraOpNumThreads(1);
        if (use_cuda) {
            try {
                OrtCUDAProviderOptions cuda{};
                opts.AppendExecutionProvider_CUDA(cuda);
            } catch (const Ort::Exception& e) {
                PM_WARN("FaceRecognizer: CUDA EP unavailable ({}), CPU fallback", e.what());
            }
        }
#ifdef _WIN32
        std::wstring wpath(path.begin(), path.end());
        return std::make_unique<Ort::Session>(env, wpath.c_str(), opts);
#else
        return std::make_unique<Ort::Session>(env, path.c_str(), opts);
#endif
    } catch (const std::exception& e) {
        PM_ERROR("FaceRecognizer: failed to load '{}': {}", path, e.what());
        return nullptr;
    }
}

float cosine(const Embedding& a, const Embedding& b) {
    if (a.size() != b.size() || a.empty())
        return -1.f;
    float dot = 0.f, na = 0.f, nb = 0.f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    if (na <= 0.f || nb <= 0.f)
        return -1.f;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

} // namespace

FaceRecognizer::FaceRecognizer() = default;
FaceRecognizer::~FaceRecognizer() = default;

bool FaceRecognizer::load(const std::string& scrfd_path, const std::string& arcface_path, bool use_cuda) {
    env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "pm_face");
    mem_info_ = std::make_unique<Ort::MemoryInfo>(
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

    scrfd_   = makeSession(*env_, scrfd_path, use_cuda);
    arcface_ = makeSession(*env_, arcface_path, use_cuda);

    Ort::AllocatorWithDefaultOptions alloc;
    if (scrfd_) {
        scrfd_in_ = scrfd_->GetInputNameAllocated(0, alloc).get();
        const size_t n = scrfd_->GetOutputCount();
        scrfd_out_.clear();
        for (size_t i = 0; i < n; ++i)
            scrfd_out_.push_back(scrfd_->GetOutputNameAllocated(i, alloc).get());
        auto in_shape = scrfd_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (in_shape.size() == 4) {
            if (in_shape[2] > 0) scrfd_h_ = static_cast<int>(in_shape[2]);
            if (in_shape[3] > 0) scrfd_w_ = static_cast<int>(in_shape[3]);
        }
        PM_INFO("FaceRecognizer: SCRFD loaded ({} outputs, {}x{})", scrfd_out_.size(), scrfd_w_, scrfd_h_);
    }
    if (arcface_) {
        arcface_in_  = arcface_->GetInputNameAllocated(0, alloc).get();
        arcface_out_ = arcface_->GetOutputNameAllocated(0, alloc).get();
        PM_INFO("FaceRecognizer: ArcFace loaded");
    }
    return scrfd_ != nullptr;
}

// --- SCRFD detection --------------------------------------------------------

std::vector<FaceBox> FaceRecognizer::detect(const cv::Mat& bgr) {
    std::vector<FaceBox> faces;
    if (!scrfd_ || bgr.empty())
        return faces;

    try {
        // Letterbox to the network input.
        const float scale = std::min(static_cast<float>(scrfd_w_) / bgr.cols,
                                     static_cast<float>(scrfd_h_) / bgr.rows);
        const int new_w = static_cast<int>(std::round(bgr.cols * scale));
        const int new_h = static_cast<int>(std::round(bgr.rows * scale));
        cv::Mat resized;
        cv::resize(bgr, resized, cv::Size(new_w, new_h));
        cv::Mat canvas(scrfd_h_, scrfd_w_, CV_8UC3, cv::Scalar(0, 0, 0));
        resized.copyTo(canvas(cv::Rect(0, 0, new_w, new_h)));

        // SCRFD preprocessing: (pixel - 127.5) / 128, RGB, CHW.
        cv::Mat rgb;
        cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);
        rgb.convertTo(rgb, CV_32FC3, 1.0 / 128.0, -127.5 / 128.0);

        std::vector<float> chw(static_cast<size_t>(3) * scrfd_h_ * scrfd_w_);
        std::array<cv::Mat, 3> ch;
        for (int c = 0; c < 3; ++c)
            ch[c] = cv::Mat(scrfd_h_, scrfd_w_, CV_32FC1, chw.data() + static_cast<size_t>(c) * scrfd_h_ * scrfd_w_);
        cv::split(rgb, ch.data());

        const std::array<int64_t, 4> dims{1, 3, scrfd_h_, scrfd_w_};
        Ort::Value in = Ort::Value::CreateTensor<float>(*mem_info_, chw.data(), chw.size(),
                                                        dims.data(), dims.size());

        const char* in_names[] = {scrfd_in_.c_str()};
        std::vector<const char*> out_names;
        for (auto& s : scrfd_out_) out_names.push_back(s.c_str());

        auto outs = scrfd_->Run(Ort::RunOptions{nullptr}, in_names, &in, 1,
                                out_names.data(), out_names.size());

        // SCRFD emits 6 (no-kps) or 9 (with-kps) outputs: score[N,1], bbox[N,4]
        // and optionally kps[N,10], one tensor per stride {8,16,32}. The export
        // order is NOT fixed — InsightFace's standard ONNX groups them by type
        // (all scores, then all bboxes, then all kps), not interleaved per
        // stride. Selecting them by a fixed stride*group offset therefore reads
        // the wrong tensors (bbox values get treated as scores → hundreds of
        // bogus "faces"). Instead, match each output to a stride by its row
        // count (anchors) and to a role by its last-dim width (1/4/10).
        const std::array<int, 3> strides{8, 16, 32};
        const int num_anchors = 2;   // SCRFD anchors per location

        std::vector<cv::Rect> nms_boxes;
        std::vector<float>     nms_scores;
        std::vector<FaceBox>   cands;

        // Index every output by (rows, width) so we can look up the right tensor.
        struct OutInfo { const float* data; int64_t rows; int64_t width; };
        std::vector<OutInfo> infos;
        infos.reserve(outs.size());
        for (auto& o : outs) {
            auto sh = o.GetTensorTypeAndShapeInfo().GetShape();
            int64_t rows = sh.size() >= 1 ? sh[0] : 0;
            int64_t width = sh.size() >= 2 ? sh[1] : 1;
            // Tolerate a leading batch dim (e.g. [1, N, w]).
            if (sh.size() == 3) { rows = sh[1]; width = sh[2]; }
            infos.push_back({o.GetTensorData<float>(), rows, width});
        }
        auto findOut = [&](int64_t rows, int64_t width) -> const float* {
            for (const auto& in : infos)
                if (in.rows == rows && in.width == width) return in.data;
            return nullptr;
        };
        const bool has_kps = outs.size() >= 9;

        for (size_t s = 0; s < strides.size(); ++s) {
            const int stride = strides[s];
            const int fw = scrfd_w_ / stride;
            const int fh = scrfd_h_ / stride;
            const int64_t rows = static_cast<int64_t>(fw) * fh * num_anchors;

            const float* scores = findOut(rows, 1);
            const float* bboxes = findOut(rows, 4);
            const float* kps    = has_kps ? findOut(rows, 10) : nullptr;
            if (!scores || !bboxes)
                continue;   // unexpected export shape for this stride; skip it

            int idx = 0;
            for (int y = 0; y < fh; ++y) {
                for (int x = 0; x < fw; ++x) {
                    for (int a = 0; a < num_anchors; ++a, ++idx) {
                        const float score = scores[idx];
                        if (score < det_threshold_)
                            continue;
                        const float ax = static_cast<float>(x * stride);
                        const float ay = static_cast<float>(y * stride);
                        // Distance-to-box decoding (ltrb * stride).
                        const float l = bboxes[idx * 4 + 0] * stride;
                        const float t = bboxes[idx * 4 + 1] * stride;
                        const float r = bboxes[idx * 4 + 2] * stride;
                        const float b = bboxes[idx * 4 + 3] * stride;
                        float x0 = (ax - l) / scale, y0 = (ay - t) / scale;
                        float x1 = (ax + r) / scale, y1 = (ay + b) / scale;

                        FaceBox fb;
                        fb.rect = cv::Rect2f(x0, y0, x1 - x0, y1 - y0);
                        fb.score = score;
                        if (kps) {
                            for (int k = 0; k < 5; ++k) {
                                fb.kps[k].x = (ax + kps[idx * 10 + k * 2 + 0] * stride) / scale;
                                fb.kps[k].y = (ay + kps[idx * 10 + k * 2 + 1] * stride) / scale;
                            }
                        }
                        cands.push_back(fb);
                        nms_boxes.emplace_back(cv::Rect(static_cast<int>(x0), static_cast<int>(y0),
                                                        static_cast<int>(x1 - x0), static_cast<int>(y1 - y0)));
                        nms_scores.push_back(score);
                    }
                }
            }
        }

        std::vector<int> keep;
        cv::dnn::NMSBoxes(nms_boxes, nms_scores, det_threshold_, 0.4f, keep);
        for (int i : keep)
            faces.push_back(cands[i]);
    } catch (const std::exception& e) {
        PM_WARN("FaceRecognizer::detect failed: {}", e.what());
    }
    return faces;
}

// --- ArcFace embedding ------------------------------------------------------

cv::Mat FaceRecognizer::alignFace(const cv::Mat& bgr, const FaceBox& face) {
    // Standard ArcFace 112x112 reference landmarks ( insightface).
    static const cv::Point2f kRef[5] = {
        {38.2946f, 51.6963f}, {73.5318f, 51.5014f}, {56.0252f, 71.7366f},
        {41.5493f, 92.3655f}, {70.7299f, 92.2041f}};

    bool have_kps = false;
    for (int i = 0; i < 5; ++i)
        if (face.kps[i].x != 0.f || face.kps[i].y != 0.f) { have_kps = true; break; }

    if (have_kps) {
        std::vector<cv::Point2f> src(face.kps, face.kps + 5);
        std::vector<cv::Point2f> dst(kRef, kRef + 5);
        cv::Mat M = cv::estimateAffinePartial2D(src, dst);
        if (!M.empty()) {
            cv::Mat out;
            cv::warpAffine(bgr, out, M, cv::Size(112, 112));
            return out;
        }
    }
    // Fall back to a plain crop+resize when landmarks are unavailable.
    cv::Rect r = cv::Rect(face.rect) & cv::Rect(0, 0, bgr.cols, bgr.rows);
    if (r.width <= 0 || r.height <= 0)
        return cv::Mat();
    cv::Mat out;
    cv::resize(bgr(r), out, cv::Size(112, 112));
    return out;
}

Embedding FaceRecognizer::runArcface(const cv::Mat& aligned112) const {
    Embedding emb;
    if (!arcface_ || aligned112.empty())
        return emb;
    try {
        cv::Mat rgb;
        cv::cvtColor(aligned112, rgb, cv::COLOR_BGR2RGB);
        rgb.convertTo(rgb, CV_32FC3, 1.0 / 127.5, -1.0);   // [-1,1]

        std::vector<float> chw(static_cast<size_t>(3) * 112 * 112);
        std::array<cv::Mat, 3> ch;
        for (int c = 0; c < 3; ++c)
            ch[c] = cv::Mat(112, 112, CV_32FC1, chw.data() + static_cast<size_t>(c) * 112 * 112);
        cv::split(rgb, ch.data());

        const std::array<int64_t, 4> dims{1, 3, 112, 112};
        Ort::Value in = Ort::Value::CreateTensor<float>(*mem_info_, chw.data(), chw.size(),
                                                        dims.data(), dims.size());
        const char* in_names[]  = {arcface_in_.c_str()};
        const char* out_names[] = {arcface_out_.c_str()};
        auto outs = arcface_->Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 1);

        const float* d = outs[0].GetTensorData<float>();
        auto shape = outs[0].GetTensorTypeAndShapeInfo().GetShape();
        size_t n = 1;
        for (auto v : shape) n *= (v > 0 ? static_cast<size_t>(v) : 1);
        emb.assign(d, d + n);

        // L2-normalize so we can compare with a plain dot product later.
        float norm = 0.f;
        for (float v : emb) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0.f)
            for (float& v : emb) v /= norm;
    } catch (const std::exception& e) {
        PM_WARN("FaceRecognizer::embed failed: {}", e.what());
        emb.clear();
    }
    return emb;
}

Embedding FaceRecognizer::embed(const cv::Mat& bgr, const FaceBox& face) {
    return runArcface(alignFace(bgr, face));
}

// --- gallery ----------------------------------------------------------------

void FaceRecognizer::setGallery(std::vector<GalleryEntry> entries) {
    std::lock_guard lk(gallery_mtx_);
    gallery_ = std::move(entries);
}

FaceMatch FaceRecognizer::match(const Embedding& probe) const {
    FaceMatch best;
    if (probe.empty())
        return best;
    std::lock_guard lk(gallery_mtx_);
    for (const auto& g : gallery_) {
        const float s = cosine(probe, g.vec);
        if (s > best.similarity) {
            best.similarity = s;
            best.user_id = g.user_id;
        }
    }
    if (best.similarity < match_threshold_)
        best.user_id = -1;   // below threshold => unknown
    return best;
}

// Gallery file format: little-endian header {magic 'PMFG', uint32 count,
// uint32 dim} followed by count*dim float32. Simple, version-1, no deps.
bool FaceRecognizer::saveGallery(const std::string& path, const std::vector<Embedding>& vecs) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        PM_ERROR("saveGallery: cannot open '{}'", path);
        return false;
    }
    const uint32_t magic = 0x47464D50u;   // 'PMFG'
    const uint32_t count = static_cast<uint32_t>(vecs.size());
    const uint32_t dim   = count ? static_cast<uint32_t>(vecs.front().size()) : kEmbedDim;
    f.write(reinterpret_cast<const char*>(&magic), sizeof magic);
    f.write(reinterpret_cast<const char*>(&count), sizeof count);
    f.write(reinterpret_cast<const char*>(&dim),   sizeof dim);
    for (const auto& v : vecs)
        f.write(reinterpret_cast<const char*>(v.data()),
                static_cast<std::streamsize>(v.size() * sizeof(float)));
    return static_cast<bool>(f);
}

std::vector<Embedding> FaceRecognizer::loadGallery(const std::string& path) {
    std::vector<Embedding> out;
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return out;
    uint32_t magic = 0, count = 0, dim = 0;
    f.read(reinterpret_cast<char*>(&magic), sizeof magic);
    f.read(reinterpret_cast<char*>(&count), sizeof count);
    f.read(reinterpret_cast<char*>(&dim),   sizeof dim);
    if (!f || magic != 0x47464D50u || dim == 0 || dim > 4096 || count > 100000)
        return out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        Embedding v(dim);
        f.read(reinterpret_cast<char*>(v.data()),
               static_cast<std::streamsize>(dim * sizeof(float)));
        if (!f)
            break;
        out.push_back(std::move(v));
    }
    return out;
}

} // namespace polymath
