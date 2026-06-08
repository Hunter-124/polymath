#include "tts_piper.h"
#include "logging.h"

#include <map>
#include <mutex>

// miniaudio implementation lives in capture.cpp; here we only use the API.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"

// Piper is gated by POLYMATH_HAVE_PIPER (set by CMake when third_party/piper is
// present). Pinned to piper 1.2.x (piper.hpp: piper::PiperConfig, piper::Voice,
// piper::initialize / loadVoice / textToAudio / terminate). Without it the
// module still compiles; speak() logs and returns false.
#if defined(POLYMATH_HAVE_PIPER)
#  include "piper.hpp"
#endif

namespace polymath::audio {

struct TtsPiper::Impl {
    std::filesystem::path voices_dir;
    std::string           default_voice;

#if defined(POLYMATH_HAVE_PIPER)
    piper::PiperConfig                            config;
    std::map<std::string, std::unique_ptr<piper::Voice>> voices;  // lazy cache
    std::mutex                                    mtx;

    // Loads (or returns cached) voice. Returns nullptr if files missing.
    piper::Voice* voice(const std::filesystem::path& voices_dir, const std::string& name) {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = voices.find(name);
        if (it != voices.end()) return it->second.get();

        const auto dir   = voices_dir / name;
        const auto model = dir / (name + ".onnx");
        const auto cfg   = dir / (name + ".onnx.json");
        if (!std::filesystem::exists(model) || !std::filesystem::exists(cfg)) {
            PM_WARN("audio.tts: voice '{}' not found under {}", name, dir.string());
            return nullptr;
        }
        auto v = std::make_unique<piper::Voice>();
        std::optional<piper::SpeakerId> sid;
        try {
            piper::loadVoice(config, model.string(), cfg.string(), *v, sid, false);
        } catch (const std::exception& e) {
            PM_ERROR("audio.tts: loadVoice('{}') failed: {}", name, e.what());
            return nullptr;
        }
        auto* raw = v.get();
        voices.emplace(name, std::move(v));
        return raw;
    }
#endif
};

TtsPiper::TtsPiper() : d_(std::make_unique<Impl>()) {}

TtsPiper::~TtsPiper() {
#if defined(POLYMATH_HAVE_PIPER)
    if (ready_) piper::terminate(d_->config);
#endif
}

bool TtsPiper::init(const std::filesystem::path& voices_dir, const std::string& default_voice) {
    d_->voices_dir    = voices_dir;
    d_->default_voice = default_voice;
    ready_            = false;
#if defined(POLYMATH_HAVE_PIPER)
    try {
        // espeak-ng data ships alongside the voices for phonemisation.
        d_->config.eSpeakDataPath = (voices_dir / "espeak-ng-data").string();
        piper::initialize(d_->config);
    } catch (const std::exception& e) {
        PM_ERROR("audio.tts: piper::initialize failed: {}", e.what());
        return false;
    }
    ready_ = true;
    PM_INFO("audio.tts: Piper ready (default voice '{}')", default_voice);
    return true;
#else
    PM_WARN("audio.tts: built without Piper; speak() is a no-op");
    return false;
#endif
}

bool TtsPiper::synthesize(const std::string& text, const std::string& voice,
                          std::vector<int16_t>& out_pcm, int& out_sample_rate) {
    out_pcm.clear();
    out_sample_rate = 22050;   // typical Piper voice rate; overwritten below
    if (!ready_ || text.empty()) return false;

#if defined(POLYMATH_HAVE_PIPER)
    const std::string name = voice.empty() ? d_->default_voice : voice;
    piper::Voice* v = d_->voice(d_->voices_dir, name);
    if (!v) {
        // Fall back to default voice if the requested one is missing.
        if (name != d_->default_voice) v = d_->voice(d_->voices_dir, d_->default_voice);
        if (!v) return false;
    }

    piper::SynthesisResult result;
    try {
        // textToAudio appends int16 samples; pass an empty audio-callback so it
        // collects the whole utterance into out_pcm.
        piper::textToAudio(d_->config, *v, text, out_pcm, result, nullptr);
    } catch (const std::exception& e) {
        PM_ERROR("audio.tts: textToAudio failed: {}", e.what());
        return false;
    }
    out_sample_rate = v->synthesisConfig.sampleRate;
    return !out_pcm.empty();
#else
    (void)text; (void)voice;
    return false;
#endif
}

bool TtsPiper::speak(const std::string& text, const std::string& voice) {
    std::vector<int16_t> pcm;
    int sr = 22050;
    if (!synthesize(text, voice, pcm, sr) || pcm.empty()) return false;

    // Blocking playback via a temporary miniaudio device + a tiny SPSC cursor.
    struct PlaybackState {
        const int16_t* data = nullptr;
        size_t         total = 0;
        size_t         pos = 0;
        bool           done = false;
    } state;
    state.data  = pcm.data();
    state.total = pcm.size();

    auto cb = [](ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames) {
        auto* st = static_cast<PlaybackState*>(dev->pUserData);
        auto* dst = static_cast<int16_t*>(out);
        ma_uint32 i = 0;
        for (; i < frames && st->pos < st->total; ++i, ++st->pos)
            dst[i] = st->data[st->pos];
        for (; i < frames; ++i) dst[i] = 0;   // pad tail with silence
        if (st->pos >= st->total) st->done = true;
    };

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_s16;
    cfg.playback.channels = 1;
    cfg.sampleRate        = static_cast<ma_uint32>(sr);
    cfg.dataCallback      = cb;
    cfg.pUserData         = &state;

    ma_device device;
    if (ma_device_init(nullptr, &cfg, &device) != MA_SUCCESS) {
        PM_ERROR("audio.tts: playback device init failed");
        return false;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        PM_ERROR("audio.tts: playback start failed");
        return false;
    }

    // Spin until the buffer drains. miniaudio drives the callback on its own
    // thread; we just wait on this (already off-UI) worker thread.
    while (!state.done) ma_sleep(10);
    ma_sleep(50);   // let the last period flush

    ma_device_uninit(&device);
    PM_DEBUG("audio.tts: spoke {} samples @ {} Hz", pcm.size(), sr);
    return true;
}

} // namespace polymath::audio
