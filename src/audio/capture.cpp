#include "capture.h"
#include "logging.h"

// miniaudio is header-only. We compile its single implementation here; this is
// the ONLY translation unit in the module that defines MINIAUDIO_IMPLEMENTATION
// so tts_piper.cpp (playback) can include miniaudio.h without re-defining it.
//
// Pinned to miniaudio 0.11.x (single-file release, miniaudio.h).
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING        // we feed raw PCM; no file decoders needed
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"

namespace polymath::audio {

struct Capture::Impl {
    ma_context  context{};
    ma_device   device{};
    bool        context_ready = false;
    bool        device_ready  = false;
    bool        running       = false;
    FloatRing*  ring          = nullptr;
};

// Realtime audio thread. miniaudio already converted to f32/mono/16k for us
// (configured below), so this is a straight lock-free copy into the ring.
static void onCapture(ma_device* dev, void* /*out*/, const void* input, ma_uint32 frames) {
    auto* impl = static_cast<Capture::Impl*>(dev->pUserData);
    if (!impl || !impl->ring || input == nullptr) return;
    impl->ring->write(static_cast<const float*>(input), static_cast<size_t>(frames));
}

Capture::Capture() : d_(std::make_unique<Impl>()) { d_->ring = &ring_; }

Capture::~Capture() {
    stop();
    if (d_->device_ready)  ma_device_uninit(&d_->device);
    if (d_->context_ready) ma_context_uninit(&d_->context);
}

bool Capture::init() {
    if (d_->device_ready) return true;

    // Prefer WASAPI on Windows; miniaudio falls back gracefully otherwise.
    ma_backend backends[] = { ma_backend_wasapi };
    if (ma_context_init(backends, 1, nullptr, &d_->context) != MA_SUCCESS) {
        // Fall back to default backend selection.
        if (ma_context_init(nullptr, 0, nullptr, &d_->context) != MA_SUCCESS) {
            PM_ERROR("audio.capture: ma_context_init failed");
            return false;
        }
    }
    d_->context_ready = true;

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format    = ma_format_f32;
    cfg.capture.channels  = kChannels;
    cfg.sampleRate        = kSampleRate;       // miniaudio resamples device->16k
    cfg.periodSizeInFrames = kFrameSamples;    // ~80 ms periods
    cfg.dataCallback      = onCapture;
    cfg.pUserData         = d_.get();

    if (ma_device_init(&d_->context, &cfg, &d_->device) != MA_SUCCESS) {
        PM_ERROR("audio.capture: ma_device_init failed (no microphone?)");
        return false;
    }
    d_->device_ready = true;
    PM_INFO("audio.capture: device ready ({} Hz mono f32)", kSampleRate);
    return true;
}

bool Capture::start() {
    if (!d_->device_ready && !init()) return false;
    if (d_->running) return true;
    if (ma_device_start(&d_->device) != MA_SUCCESS) {
        PM_ERROR("audio.capture: ma_device_start failed");
        return false;
    }
    ring_.clear();
    d_->running = true;
    PM_INFO("audio.capture: streaming");
    return true;
}

void Capture::stop() {
    if (!d_->running) return;
    ma_device_stop(&d_->device);
    d_->running = false;
    ring_.clear();
    PM_INFO("audio.capture: stopped");
}

bool Capture::isRunning() const { return d_->running; }

} // namespace polymath::audio
