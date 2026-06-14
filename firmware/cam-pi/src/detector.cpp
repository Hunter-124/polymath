#include "hearth_pi/detector.h"
#include <cstdlib>
#include <cstring>
#include <vector>

namespace hearth {

struct PersonDetector::Impl {
    bool ready = false;
    // motion-stub state: previous coarse luma map
    std::vector<uint8_t> prev;
};

PersonDetector::PersonDetector(std::string modelPath)
    : modelPath_(std::move(modelPath)) { impl_ = new Impl(); }

PersonDetector::~PersonDetector() { delete impl_; }

bool PersonDetector::begin() {
#ifdef HEARTH_HAVE_ONNX
    // TODO(model): create an Ort::Session over modelPath_ (yolov8n), set up the
    // input/output names, and mark ready. detect() then letterboxes the RGB
    // frame to 640x640, runs the session, and parses class-0 (person) boxes.
    // This mirrors the hub's vision pipeline (reuse src/vision if linked).
    impl_->ready = false;   // flip true once the session is wired
#else
    impl_->ready = false;
#endif
    return true;   // never fail startup; degrade to motion
}

const char* PersonDetector::backend() const {
#ifdef HEARTH_HAVE_HAILO
    return impl_->ready ? "hailo" : "motion-stub";
#elif defined(HEARTH_HAVE_ONNX)
    return impl_->ready ? "onnxruntime" : "motion-stub";
#else
    return "motion-stub";
#endif
}

Detection PersonDetector::detect(const Frame& f) {
    Detection d;
    if (impl_->ready) {
        // TODO(model): run inference and return the top person box.
        return d;
    }
    // Motion-stub fallback: 32x24 coarse luma diff over the RGB frame.
    if (!f.rgb || f.width <= 0 || f.height <= 0) return d;
    const int GW = 32, GH = 24, N = GW * GH;
    std::vector<uint8_t> cur(N);
    for (int gy = 0; gy < GH; ++gy)
        for (int gx = 0; gx < GW; ++gx) {
            int px = (gx * f.width) / GW, py = (gy * f.height) / GH;
            const uint8_t* p = f.rgb + (py * f.width + px) * 3;
            cur[gy * GW + gx] = (uint8_t)((p[0] * 30 + p[1] * 59 + p[2] * 11) / 100); // luma
        }
    if (impl_->prev.size() != (size_t)N) { impl_->prev = cur; return d; }
    int changed = 0;
    for (int i = 0; i < N; ++i)
        if (std::abs((int)cur[i] - (int)impl_->prev[i]) > 24) changed++;
    impl_->prev = cur;
    float frac = (float)changed / N;
    // Reported as confidence; the service maps a positive to kind="motion".
    d.person = false;
    d.confidence = (frac >= 0.04f) ? frac : 0.0f;
    return d;
}

} // namespace hearth
