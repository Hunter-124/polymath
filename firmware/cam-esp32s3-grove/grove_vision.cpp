#include "grove_vision.h"
#include <Wire.h>

#ifdef HEARTH_HAVE_SSCMA
#include <Seeed_Arduino_SSCMA.h>
static SSCMA s_ai;
#endif

namespace hearth {

// COCO "person" is class id 0 in the standard YOLOv8 person/coco models Seeed ship.
static constexpr int kPersonClass = 0;

bool GroveVision::begin(uint8_t i2cAddr, int sda, int scl) {
    Wire.begin(sda, scl);
#ifdef HEARTH_HAVE_SSCMA
    // SSCMA over I2C; address default 0x62. begin() returns 0 on success.
    ready_ = (s_ai.begin(&Wire, i2cAddr) == 0);
    return ready_;
#else
    // TODO(model): add Seeed_Arduino_SSCMA + define HEARTH_HAVE_SSCMA to enable
    // real YOLOv8 person detection on the Grove Vision AI V2. Stubbed here.
    (void)i2cAddr;
    ready_ = false;
    return false;
#endif
}

GroveDet GroveVision::poll() {
    GroveDet d;
#ifdef HEARTH_HAVE_SSCMA
    if (!ready_) return d;
    if (s_ai.invoke(1, false, true) != 0) return d;   // one inference, keep image
    // Walk detected boxes; keep the highest-score person.
    for (const auto& b : s_ai.boxes()) {
        if (b.target == kPersonClass) {
            float c = b.score / 100.0f;               // SSCMA score is 0..100
            if (c > d.confidence) { d.confidence = c; d.person = true; }
        }
    }
    // Cache the JPEG preview the module returns alongside the inference.
    if (s_ai.last_image().length()) {
        jpeg_    = (const uint8_t*)s_ai.last_image().c_str();
        jpegLen_ = s_ai.last_image().length();
    }
#endif
    return d;
}

const uint8_t* GroveVision::lastJpeg(size_t& len) const {
    len = jpegLen_;
    return jpeg_;
}

} // namespace hearth
