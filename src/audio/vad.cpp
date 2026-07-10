#include "vad.h"
#include "onnx_util.h"
#include "audio_common.h"
#include "logging.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

// Silero VAD v5 ONNX interface (silero-vad 4.0+ unified export):
//   inputs : input [1,512] float, state [2,1,128] float, sr int64 (16000)
//   outputs: output [1,1] float (speech prob), stateN [2,1,128] float

namespace polymath::audio {

namespace {
constexpr int kWindow      = 512;
constexpr int kStateSize   = 2 * 1 * 128;
constexpr int kWindowMs    = 32;   // 512 / 16000 * 1000
// Backoff schedule: 1 s, 5 s, 30 s — three attempts then permanent fail + Notice.
constexpr int kBackoffSec[] = {1, 5, 30};
constexpr int kMaxReloads   = 3;

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
}

struct Vad::Impl {
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo mem{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};

    std::array<float, kStateSize> state{};
    int64_t sr = kSampleRate;

    std::vector<Ort::AllocatedStringPtr> in_names_owned;
    std::vector<Ort::AllocatedStringPtr> out_names_owned;
    std::vector<const char*> in_names;
    std::vector<const char*> out_names;

    void clearNames() {
        in_names_owned.clear();
        out_names_owned.clear();
        in_names.clear();
        out_names.clear();
    }
};

Vad::Vad() : d_(std::make_unique<Impl>()) {}
Vad::~Vad() = default;

bool Vad::load(const std::filesystem::path& model_path) {
    model_path_ = model_path;
    ready_ = false;
    reload_attempts_ = 0;
    next_retry_ms_ = 0;
    d_->session.reset();
    d_->clearNames();

    if (!std::filesystem::exists(model_path)) {
        PM_WARN("audio.vad: model missing: {} (VAD disabled)", model_path.string());
        return false;
    }
    try {
        auto opts = defaultSessionOptions();
        d_->session = std::make_unique<Ort::Session>(
            ortEnv(), ortPath(model_path).c_str(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        size_t n_in  = d_->session->GetInputCount();
        size_t n_out = d_->session->GetOutputCount();
        for (size_t i = 0; i < n_in; ++i) {
            d_->in_names_owned.push_back(d_->session->GetInputNameAllocated(i, alloc));
            d_->in_names.push_back(d_->in_names_owned.back().get());
        }
        for (size_t i = 0; i < n_out; ++i) {
            d_->out_names_owned.push_back(d_->session->GetOutputNameAllocated(i, alloc));
            d_->out_names.push_back(d_->out_names_owned.back().get());
        }
    } catch (const Ort::Exception& e) {
        PM_ERROR("audio.vad: load failed: {}", e.what());
        return false;
    }
    reset();
    ready_ = true;
    PM_INFO("audio.vad: loaded ({} inputs)", d_->in_names.size());
    return true;
}

bool Vad::tryReload() {
    if (model_path_.empty()) return false;
    PM_WARN("audio.vad: reloading session (attempt {}/{})",
            reload_attempts_ + 1, kMaxReloads);
    d_->session.reset();
    d_->clearNames();
    const bool ok = load(model_path_);
    // load() resets reload_attempts_; restore count so onInferenceError can track.
    return ok;
}

void Vad::onInferenceError(const char* what) {
    PM_ERROR("audio.vad: inference error: {}", what ? what : "?");
    ready_ = false;
    d_->session.reset();

    if (reload_attempts_ >= kMaxReloads) {
        PM_ERROR("audio.vad: reload budget exhausted; VAD disabled");
        if (notice_fn_)
            notice_fn_("error", "audio.vad",
                       "Silero VAD failed after 3 reload attempts; wake/command gating disabled");
        return;
    }

    const int delay = kBackoffSec[std::min(reload_attempts_, kMaxReloads - 1)];
    next_retry_ms_ = nowMs() + int64_t(delay) * 1000;
    ++reload_attempts_;
    PM_WARN("audio.vad: will retry session reload in {}s", delay);
}

void Vad::reset() {
    d_->state.fill(0.0f);
    in_speech_  = false;
    silence_ms_ = 0;
}

Vad::Event Vad::process(const float* window512, float* prob) {
    // Scheduled reload after a previous failure.
    if (!ready_ && next_retry_ms_ > 0 && nowMs() >= next_retry_ms_) {
        next_retry_ms_ = 0;
        const int saved_attempts = reload_attempts_;
        if (tryReload()) {
            // Successful reload: keep attempt counter so a flapping session still
            // exhausts the budget; only a clean load() from outside zeros it.
            reload_attempts_ = saved_attempts;
        } else if (reload_attempts_ >= kMaxReloads) {
            if (notice_fn_)
                notice_fn_("error", "audio.vad",
                           "Silero VAD failed after 3 reload attempts; wake/command gating disabled");
        } else {
            reload_attempts_ = saved_attempts + 1;
            const int delay = kBackoffSec[std::min(reload_attempts_ - 1, kMaxReloads - 1)];
            next_retry_ms_ = nowMs() + int64_t(delay) * 1000;
        }
    }

    if (!ready_ || window512 == nullptr) return Event::None;

    float p = 0.0f;
    try {
        std::array<int64_t, 2> in_shape{1, kWindow};
        std::array<int64_t, 3> st_shape{2, 1, 128};
        std::array<int64_t, 1> sr_shape{1};

        std::vector<Ort::Value> inputs;
        inputs.emplace_back(Ort::Value::CreateTensor<float>(
            d_->mem, const_cast<float*>(window512), kWindow, in_shape.data(), in_shape.size()));
        inputs.emplace_back(Ort::Value::CreateTensor<float>(
            d_->mem, d_->state.data(), d_->state.size(), st_shape.data(), st_shape.size()));
        if (d_->in_names.size() >= 3)
            inputs.emplace_back(Ort::Value::CreateTensor<int64_t>(
                d_->mem, &d_->sr, 1, sr_shape.data(), sr_shape.size()));

        auto out = d_->session->Run(Ort::RunOptions{nullptr},
            d_->in_names.data(), inputs.data(), inputs.size(),
            d_->out_names.data(), d_->out_names.size());

        p = out[0].GetTensorData<float>()[0];
        if (out.size() > 1) {
            const float* st = out[1].GetTensorData<float>();
            std::memcpy(d_->state.data(), st, kStateSize * sizeof(float));
        }
        ++inference_count_;
        // Healthy inference clears the backoff counter.
        reload_attempts_ = 0;
        next_retry_ms_ = 0;
    } catch (const Ort::Exception& e) {
        onInferenceError(e.what());
        return Event::None;
    }
    if (prob) *prob = p;

    // Hysteresis state machine with min-silence to bridge short pauses.
    const bool speech = p >= threshold_;
    if (speech) {
        silence_ms_ = 0;
        if (!in_speech_) { in_speech_ = true; return Event::SpeechStart; }
    } else if (in_speech_) {
        silence_ms_ += kWindowMs;
        if (silence_ms_ >= min_silence_ms_) {
            in_speech_  = false;
            silence_ms_ = 0;
            return Event::SpeechEnd;
        }
    }
    return Event::None;
}

} // namespace polymath::audio
