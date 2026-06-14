#pragma once
// PersonDetector — clean interface so the camera pipeline is detector-agnostic.
//
// Budget tier (XIAO ESP32-S3 Sense) runs on-device person detection via Espressif
// ESP-DL (a FOMO / Swift-YOLO person model). If the model blob can't be fetched in
// this build, EspDlPersonDetector compiles as a documented stub that always
// reports "no person", and the pipeline falls back to the motion gate. Swap in the
// real model by dropping the ESP-DL model headers and flipping HEARTH_HAVE_ESPDL.

#include <Arduino.h>
#include "esp_camera.h"

namespace hearth {

struct Detection {
    bool  person     = false;
    float confidence = 0.0f;   // 0..1
};

class PersonDetector {
public:
    virtual ~PersonDetector() = default;
    virtual bool begin() = 0;
    // Run on a camera frame (RGB565/JPEG handled internally per impl).
    virtual Detection detect(const camera_fb_t* fb) = 0;
    virtual const char* name() const = 0;
};

// Motion-only fallback: frame-difference gate over downscaled luma. Always
// available, never claims "person" with high confidence (reports as a trigger).
class MotionDetector : public PersonDetector {
public:
    bool begin() override { return true; }
    Detection detect(const camera_fb_t* fb) override;
    const char* name() const override { return "motion"; }
    void setThreshold(float t) { motionFrac_ = t; }
private:
    uint8_t  prev_[32 * 24] = {0};   // QQVGA-ish luma map
    bool     have_ = false;
    float    motionFrac_ = 0.04f;    // >4% changed cells => motion
};

// ESP-DL person detector (real model when HEARTH_HAVE_ESPDL is defined; stub
// otherwise). Confidence threshold is applied by the caller (EdgeConfig).
class EspDlPersonDetector : public PersonDetector {
public:
    bool begin() override;
    Detection detect(const camera_fb_t* fb) override;
    const char* name() const override;
private:
    MotionDetector fallback_;        // used when the model is stubbed out
    bool modelReady_ = false;
};

} // namespace hearth
