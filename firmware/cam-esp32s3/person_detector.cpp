#include "person_detector.h"

namespace hearth {

// --- MotionDetector ---------------------------------------------------------
// Decodes nothing fancy: samples the JPEG's luma via a coarse downscale. For
// JPEG frame buffers we approximate motion by hashing 16x16 tiles of the byte
// stream — cheap and good enough as a trigger gate; the real signal is the
// person model. (When the camera is configured for RGB565/GRAYSCALE this reads
// pixels directly.)
Detection MotionDetector::detect(const camera_fb_t* fb) {
    Detection d;
    if (!fb || !fb->buf || fb->len < sizeof(prev_)) return d;

    // Build a 32x24 coarse map by striding the frame buffer.
    uint8_t cur[32 * 24];
    size_t stride = fb->len / (32 * 24);
    if (stride == 0) stride = 1;
    for (int i = 0; i < 32 * 24; ++i) {
        size_t off = (size_t)i * stride;
        cur[i] = (off < fb->len) ? fb->buf[off] : 0;
    }

    if (!have_) { memcpy(prev_, cur, sizeof(cur)); have_ = true; return d; }

    int changed = 0;
    for (int i = 0; i < 32 * 24; ++i)
        if (abs((int)cur[i] - (int)prev_[i]) > 24) changed++;
    memcpy(prev_, cur, sizeof(cur));

    // confidence is a motion score (caller maps a positive to kind="motion");
    // person stays false because motion alone can't confirm a person.
    float frac = (float)changed / (32.0f * 24.0f);
    d.person = false;
    d.confidence = (frac >= motionFrac_) ? frac : 0.0f;
    return d;
}

// --- EspDlPersonDetector ----------------------------------------------------

bool EspDlPersonDetector::begin() {
    fallback_.begin();
#ifdef HEARTH_HAVE_ESPDL
    // TODO(model): initialise the ESP-DL person model here, e.g.
    //   #include "human_face_detect_msr01.hpp" / a FOMO person model wrapper.
    //   model_ = new PersonDetectModel();  modelReady_ = (model_ != nullptr);
    // Espressif's esp-dl exposes a `dl::detect` pipeline that takes an RGB888
    // image and returns boxes+scores. Convert the camera RGB565 frame to RGB888,
    // run it, and set Detection{person, confidence} from the top "person" box.
    modelReady_ = false;   // until the blob is wired in
#else
    modelReady_ = false;
#endif
    return true;   // never fail boot; we degrade to motion
}

Detection EspDlPersonDetector::detect(const camera_fb_t* fb) {
    if (modelReady_) {
        // TODO(model): run the ESP-DL inference and return its person box.
        return Detection{};
    }
    // Fallback: motion gate. Pipeline treats a positive as kind="motion" since we
    // cannot confirm a person on this tier without the model.
    return fallback_.detect(fb);
}

const char* EspDlPersonDetector::name() const {
    return modelReady_ ? "esp-dl-person" : "esp-dl-stub(motion)";
}

} // namespace hearth
