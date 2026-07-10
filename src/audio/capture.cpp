#include "capture.h"
#include "logging.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

// miniaudio is header-only. We compile its single implementation here; this is
// the ONLY translation unit in the module that defines MINIAUDIO_IMPLEMENTATION
// so tts_piper.cpp (playback) can include miniaudio.h without re-defining it.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
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

// Realtime audio thread — lock-free ring write only.
static void onCapture(ma_device* dev, void* /*out*/, const void* input, ma_uint32 frames) {
    auto* impl = static_cast<Capture::Impl*>(dev->pUserData);
    if (!impl || !impl->ring || input == nullptr) return;
    impl->ring->write(static_cast<const float*>(input), static_cast<size_t>(frames));
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Resolve a capture device id by name substring. Returns true and writes into
// *out_id when a match is found; false means "use default" (empty name or no
// match — we fall back rather than failing hard).
static bool resolveCaptureDevice(ma_context* ctx, const std::string& name,
                                 ma_device_id* out_id, std::string* matched_name) {
    if (name.empty() || !ctx || !out_id) return false;

    ma_device_info* pPlayback = nullptr;
    ma_uint32 nPlayback = 0;
    ma_device_info* pCapture = nullptr;
    ma_uint32 nCapture = 0;
    if (ma_context_get_devices(ctx, &pPlayback, &nPlayback, &pCapture, &nCapture) != MA_SUCCESS)
        return false;

    const std::string needle = lower(name);
    for (ma_uint32 i = 0; i < nCapture; ++i) {
        const std::string dev = lower(pCapture[i].name);
        if (dev.find(needle) != std::string::npos || needle.find(dev) != std::string::npos) {
            *out_id = pCapture[i].id;
            if (matched_name) *matched_name = pCapture[i].name;
            return true;
        }
    }
    PM_WARN("audio.capture: no capture device matching '{}'; using default", name);
    return false;
}

Capture::Capture() : d_(std::make_unique<Impl>()) { d_->ring = &ring_; }

Capture::~Capture() {
    stop();
    if (d_->device_ready)  ma_device_uninit(&d_->device);
    if (d_->context_ready) ma_context_uninit(&d_->context);
}

bool Capture::init(const std::string& device_name) {
    if (d_->device_ready && device_name == device_name_) return true;
    if (d_->device_ready) {
        // Different device requested while already inited — reinit path.
        return reinit(device_name);
    }

    device_name_ = device_name;

    // Prefer WASAPI on Windows; miniaudio falls back gracefully otherwise.
    ma_backend backends[] = { ma_backend_wasapi };
    if (ma_context_init(backends, 1, nullptr, &d_->context) != MA_SUCCESS) {
        if (ma_context_init(nullptr, 0, nullptr, &d_->context) != MA_SUCCESS) {
            PM_ERROR("audio.capture: ma_context_init failed");
            return false;
        }
    }
    d_->context_ready = true;

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format     = ma_format_f32;
    cfg.capture.channels   = kChannels;
    cfg.sampleRate         = kSampleRate;
    cfg.periodSizeInFrames = kFrameSamples;
    cfg.dataCallback       = onCapture;
    cfg.pUserData          = d_.get();

    ma_device_id chosen_id{};
    std::string matched;
    if (resolveCaptureDevice(&d_->context, device_name_, &chosen_id, &matched)) {
        cfg.capture.pDeviceID = &chosen_id;
        PM_INFO("audio.capture: selecting device '{}'", matched);
    }

    if (ma_device_init(&d_->context, &cfg, &d_->device) != MA_SUCCESS) {
        PM_ERROR("audio.capture: ma_device_init failed (no microphone?)");
        return false;
    }
    d_->device_ready = true;
    PM_INFO("audio.capture: device ready ({} Hz mono f32, ring {} samples / ~{:.1f}s)",
            kSampleRate, ring_.capacity(),
            static_cast<double>(ring_.capacity()) / kSampleRate);
    return true;
}

bool Capture::reinit(const std::string& device_name) {
    const bool was_running = d_->running;
    stop();
    if (d_->device_ready) {
        ma_device_uninit(&d_->device);
        std::memset(&d_->device, 0, sizeof(d_->device));
        d_->device_ready = false;
    }
    // Keep the context; only the device changes.
    if (!d_->context_ready) {
        device_name_.clear();
        return init(device_name);
    }
    device_name_ = device_name;

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format     = ma_format_f32;
    cfg.capture.channels   = kChannels;
    cfg.sampleRate         = kSampleRate;
    cfg.periodSizeInFrames = kFrameSamples;
    cfg.dataCallback       = onCapture;
    cfg.pUserData          = d_.get();

    ma_device_id chosen_id{};
    std::string matched;
    if (resolveCaptureDevice(&d_->context, device_name_, &chosen_id, &matched)) {
        cfg.capture.pDeviceID = &chosen_id;
        PM_INFO("audio.capture: reinit selecting device '{}'", matched);
    }

    if (ma_device_init(&d_->context, &cfg, &d_->device) != MA_SUCCESS) {
        PM_ERROR("audio.capture: reinit ma_device_init failed");
        return false;
    }
    d_->device_ready = true;
    if (was_running) return start();
    return true;
}

bool Capture::start() {
    if (!d_->device_ready && !init(device_name_)) return false;
    if (d_->running) return true;
    if (ma_device_start(&d_->device) != MA_SUCCESS) {
        PM_ERROR("audio.capture: ma_device_start failed");
        return false;
    }
    ring_.clear();
    ring_.resetDrops();
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
