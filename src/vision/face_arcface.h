#pragma once
//
// FaceRecognizer — SCRFD face detection + ArcFace embedding, both ONNX Runtime
// (CUDA EP, CPU fallback).  Pinned to ONNX Runtime 1.17.x.
//
// Models (InsightFace exports, placed under <models>/vision/):
//   scrfd_500m.onnx   — input 1x3x640x640, SCRFD multi-level outputs
//                       (scores/bbox/kps for strides 8/16/32).
//   arcface_r100.onnx — input 1x3x112x112, output 1x512 embedding.
//
// The enrolled gallery is a flat binary file of 512-float vectors per user,
// stored at <media>/faces/<user_id>.gallery and referenced by users.face_gallery.
// Matching is cosine similarity against every enrolled vector; the best score
// above kMatchThreshold wins.
//
#include "types.h"

#include <opencv2/core.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Ort { struct Env; struct Session; struct MemoryInfo; }

namespace polymath {

// One detected face: image-space bbox plus 5 alignment landmarks.
struct FaceBox {
    cv::Rect2f  rect;
    cv::Point2f kps[5];      // left eye, right eye, nose, left mouth, right mouth
    float       score = 0.f;
};

// A resolved identity for a face crop.
struct FaceMatch {
    int64_t user_id = -1;    // -1 == unknown
    float   similarity = 0.f;
};

class FaceRecognizer {
public:
    FaceRecognizer();
    ~FaceRecognizer();

    // Loads both ONNX models. Either may fail independently; detect()/embed()
    // no-op when their model is absent. Returns true if at least detection loaded.
    bool load(const std::string& scrfd_path, const std::string& arcface_path, bool use_cuda = true);
    bool detectorReady() const { return scrfd_ != nullptr; }
    bool embedderReady() const { return arcface_ != nullptr; }

    // Detect faces in a BGR frame (image-pixel coords).
    std::vector<FaceBox> detect(const cv::Mat& bgr);

    // Align (via the 5 landmarks) + embed one face to a normalized 512-vector.
    // Returns empty if the embedder is not loaded.
    Embedding embed(const cv::Mat& bgr, const FaceBox& face);

    // --- enrolled gallery ---------------------------------------------------
    struct GalleryEntry { int64_t user_id; Embedding vec; };

    // Replaces the in-memory gallery with `entries`. Called by VisionService
    // after (re)loading users from the DB.
    void setGallery(std::vector<GalleryEntry> entries);

    // Best cosine match for an embedding; user_id == -1 when below threshold.
    FaceMatch match(const Embedding& probe) const;

    // Gallery file IO helpers (used by enrollment + load).
    static bool          saveGallery(const std::string& path, const std::vector<Embedding>& vecs);
    static std::vector<Embedding> loadGallery(const std::string& path);

    void  setMatchThreshold(float t) { match_threshold_ = t; }
    float matchThreshold() const     { return match_threshold_; }

private:
    Embedding runArcface(const cv::Mat& aligned112) const;
    static cv::Mat alignFace(const cv::Mat& bgr, const FaceBox& face);

    std::unique_ptr<Ort::Env>        env_;
    std::unique_ptr<Ort::Session>    scrfd_;
    std::unique_ptr<Ort::Session>    arcface_;
    std::unique_ptr<Ort::MemoryInfo> mem_info_;

    std::string scrfd_in_, arcface_in_;
    std::vector<std::string> scrfd_out_;
    std::string arcface_out_;
    int scrfd_w_ = 640, scrfd_h_ = 640;

    // gallery_ is swapped by the service thread (enroll/load) while worker
    // threads call match(); guard it with a small mutex.
    mutable std::mutex        gallery_mtx_;
    std::vector<GalleryEntry> gallery_;
    float det_threshold_ = 0.5f;
    float match_threshold_ = 0.35f;   // cosine similarity; ~ArcFace operating point
    static constexpr int kEmbedDim = 512;
};

} // namespace polymath
