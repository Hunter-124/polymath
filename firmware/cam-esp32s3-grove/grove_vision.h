#pragma once
// grove_vision — wraps Seeed SSCMA to read YOLOv8 person detections + the JPEG
// preview from the Grove Vision AI Module V2 over I2C. The Grove module owns the
// camera + the model; the XIAO ESP32-S3 is the Wi-Fi host that publishes events.
//
// If the SSCMA library / module isn't present at build time (HEARTH_HAVE_SSCMA
// undefined), this compiles as a stub that reports no detection and an empty
// preview, so the project still builds and runs the fabric plumbing.

#include <Arduino.h>

namespace hearth {

struct GroveDet {
    bool  person     = false;
    float confidence = 0.0f;
};

class GroveVision {
public:
    bool begin(uint8_t i2cAddr, int sda, int scl);

    // Pull the latest inference; returns the top "person" box (class 0 in the
    // COCO person model). Non-blocking; returns {false,0} if nothing fresh.
    GroveDet poll();

    // Latest JPEG preview from the module (for /snapshot, thumbnails, clip
    // frames). Returns nullptr/0 if none available. Buffer owned by the wrapper.
    const uint8_t* lastJpeg(size_t& len) const;

    bool ready() const { return ready_; }

private:
    bool   ready_ = false;
    const uint8_t* jpeg_ = nullptr;
    size_t jpegLen_ = 0;
};

} // namespace hearth
