#pragma once
// PersonDetector — YOLO person detection. The flagship reuses the hub's
// ONNX/YOLO approach (see src/vision in the hub). If ONNX Runtime / a model isn't
// available at build time (HEARTH_HAVE_ONNX undefined) this links a documented
// stub that reports no person, so the service still builds and runs the fabric +
// NVR plumbing. With the Hailo AI HAT, swap in the HailoRT backend behind the
// same interface.

#include <cstdint>
#include <string>
#include <vector>

namespace hearth {

struct Detection {
    bool  person     = false;
    float confidence = 0.0f;
};

// A decoded frame (RGB888, packed) handed to the detector.
struct Frame {
    int width = 0, height = 0;
    const uint8_t* rgb = nullptr;   // width*height*3
};

class PersonDetector {
public:
    explicit PersonDetector(std::string modelPath);
    ~PersonDetector();
    bool begin();
    Detection detect(const Frame& f);
    const char* backend() const;    // "onnxruntime" | "hailo" | "motion-stub"
private:
    struct Impl;
    Impl* impl_ = nullptr;
    std::string modelPath_;
};

} // namespace hearth
