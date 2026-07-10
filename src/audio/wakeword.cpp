#include "wakeword.h"
#include "audio_common.h"
#include "onnx_util.h"
#include "logging.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

// openWakeWord ONNX front-end. Tensor shapes / window sizes follow upstream
// openWakeWord 0.6.x model exports.

namespace polymath::audio {

namespace {
constexpr int   kMelBins          = 32;
constexpr int   kEmbWindowMels    = 76;
constexpr int   kEmbDim           = 96;
constexpr int   kClsWindowEmb     = 16;
constexpr int   kMelHopSamples    = 1280;
constexpr int   kRefractoryFrames = 12;
constexpr int   kBackoffSec[]     = {1, 5, 30};
constexpr int   kMaxReloads       = 3;
// Cap feature history without hot erase of large ranges.
constexpr int   kMaxMelFrames     = 96;
constexpr int   kMaxEmbFrames     = 32;

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
}

struct WakeWord::Impl {
    std::unique_ptr<Ort::Session> mel;
    std::unique_ptr<Ort::Session> emb;
    std::unique_ptr<Ort::Session> cls;
    Ort::MemoryInfo mem{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};

    std::string mel_in, mel_out, emb_in, emb_out, cls_in, cls_out;

    // Fixed-capacity rolling feature buffers (index, no hot erase).
    std::array<float, kMelBins> mel_buf[kMaxMelFrames]{};
    int mel_head = 0;
    int mel_size = 0;

    std::array<float, kEmbDim> emb_buf[kMaxEmbFrames]{};
    int emb_head = 0;
    int emb_size = 0;

    // Leftover raw samples toward the next 1280 hop — fixed ring, no erase.
    float audio_accum[kMelHopSamples * 2]{};
    int   audio_fill = 0;

    void clearFeatures() {
        mel_head = mel_size = 0;
        emb_head = emb_size = 0;
        audio_fill = 0;
    }

    void pushMel(const std::array<float, kMelBins>& row) {
        mel_buf[mel_head] = row;
        mel_head = (mel_head + 1) % kMaxMelFrames;
        if (mel_size < kMaxMelFrames) ++mel_size;
    }

    // Chronological mel row i (0 = oldest).
    const std::array<float, kMelBins>& melAt(int i) const {
        const int start = (mel_head - mel_size + kMaxMelFrames) % kMaxMelFrames;
        return mel_buf[(start + i) % kMaxMelFrames];
    }

    void dropOldestMels(int n) {
        n = std::min(n, mel_size);
        mel_size -= n;
    }

    void pushEmb(const std::array<float, kEmbDim>& e) {
        emb_buf[emb_head] = e;
        emb_head = (emb_head + 1) % kMaxEmbFrames;
        if (emb_size < kMaxEmbFrames) ++emb_size;
    }

    const std::array<float, kEmbDim>& embAt(int i) const {
        const int start = (emb_head - emb_size + kMaxEmbFrames) % kMaxEmbFrames;
        return emb_buf[(start + i) % kMaxEmbFrames];
    }
};

WakeWord::WakeWord() : d_(std::make_unique<Impl>()) {}
WakeWord::~WakeWord() = default;

static std::unique_ptr<Ort::Session> loadSession(const std::filesystem::path& p) {
    if (!std::filesystem::exists(p)) {
        PM_WARN("audio.wakeword: model missing: {}", p.string());
        return nullptr;
    }
    try {
        auto opts = defaultSessionOptions();
        return std::make_unique<Ort::Session>(ortEnv(), ortPath(p).c_str(), opts);
    } catch (const Ort::Exception& e) {
        PM_ERROR("audio.wakeword: failed to load {}: {}", p.string(), e.what());
        return nullptr;
    }
}

bool WakeWord::load(const std::filesystem::path& model_dir, const std::string& phrase) {
    model_dir_ = model_dir;
    phrase_ = phrase;
    ready_  = false;
    reload_attempts_ = 0;
    next_retry_ms_ = 0;

    d_->mel = loadSession(model_dir / "melspectrogram.onnx");
    d_->emb = loadSession(model_dir / "embedding_model.onnx");
    d_->cls = loadSession(model_dir / (phrase + ".onnx"));
    if (!d_->mel || !d_->emb || !d_->cls) {
        PM_WARN("audio.wakeword: detector disabled (missing models for '{}')", phrase);
        return false;
    }

    Ort::AllocatorWithDefaultOptions alloc;
    d_->mel_in  = d_->mel->GetInputNameAllocated(0, alloc).get();
    d_->mel_out = d_->mel->GetOutputNameAllocated(0, alloc).get();
    d_->emb_in  = d_->emb->GetInputNameAllocated(0, alloc).get();
    d_->emb_out = d_->emb->GetOutputNameAllocated(0, alloc).get();
    d_->cls_in  = d_->cls->GetInputNameAllocated(0, alloc).get();
    d_->cls_out = d_->cls->GetOutputNameAllocated(0, alloc).get();

    reset();
    ready_ = true;
    PM_INFO("audio.wakeword: loaded '{}' (threshold {:.2f})", phrase, threshold_);
    return true;
}

bool WakeWord::tryReload() {
    if (model_dir_.empty() || phrase_.empty()) return false;
    PM_WARN("audio.wakeword: reloading sessions (attempt {}/{})",
            reload_attempts_ + 1, kMaxReloads);
    d_->mel.reset();
    d_->emb.reset();
    d_->cls.reset();
    return load(model_dir_, phrase_);
}

void WakeWord::onInferenceError(const char* what) {
    PM_ERROR("audio.wakeword: inference error: {}", what ? what : "?");
    ready_ = false;
    d_->mel.reset();
    d_->emb.reset();
    d_->cls.reset();

    if (reload_attempts_ >= kMaxReloads) {
        PM_ERROR("audio.wakeword: reload budget exhausted; detector disabled");
        if (notice_fn_)
            notice_fn_("error", "audio.wakeword",
                       "Wake-word detector failed after 3 reload attempts");
        return;
    }
    const int delay = kBackoffSec[std::min(reload_attempts_, kMaxReloads - 1)];
    next_retry_ms_ = nowMs() + int64_t(delay) * 1000;
    ++reload_attempts_;
    PM_WARN("audio.wakeword: will retry session reload in {}s", delay);
}

void WakeWord::reset() {
    d_->clearFeatures();
    refractory_left_ = 0;
}

static void runMel(WakeWord::Impl& d, const float* samples, int n) {
    std::array<int64_t, 2> shape{1, n};
    Ort::Value in = Ort::Value::CreateTensor<float>(
        d.mem, const_cast<float*>(samples), static_cast<size_t>(n), shape.data(), shape.size());
    const char* in_names[]  = { d.mel_in.c_str() };
    const char* out_names[] = { d.mel_out.c_str() };
    auto out = d.mel->Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 1);

    auto info = out[0].GetTensorTypeAndShapeInfo();
    auto dims = info.GetShape();
    const float* data = out[0].GetTensorData<float>();
    const int frames = static_cast<int>(dims[dims.size() - 2]);
    for (int f = 0; f < frames; ++f) {
        std::array<float, kMelBins> row;
        for (int b = 0; b < kMelBins; ++b)
            row[b] = data[f * kMelBins + b] / 10.0f + 2.0f;
        d.pushMel(row);
    }
}

static void runEmbeddings(WakeWord::Impl& d) {
    while (d.mel_size >= kEmbWindowMels) {
        std::vector<float> win(static_cast<size_t>(kEmbWindowMels * kMelBins));
        for (int f = 0; f < kEmbWindowMels; ++f) {
            const auto& row = d.melAt(f);
            std::copy(row.begin(), row.end(), win.begin() + f * kMelBins);
        }

        std::array<int64_t, 4> shape{1, kEmbWindowMels, kMelBins, 1};
        Ort::Value in = Ort::Value::CreateTensor<float>(
            d.mem, win.data(), win.size(), shape.data(), shape.size());
        const char* in_names[]  = { d.emb_in.c_str() };
        const char* out_names[] = { d.emb_out.c_str() };
        auto out = d.emb->Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 1);

        const float* edata = out[0].GetTensorData<float>();
        std::array<float, kEmbDim> e;
        std::copy(edata, edata + kEmbDim, e.begin());
        d.pushEmb(e);

        d.dropOldestMels(8);   // hop by 8 mel frames (oWW: 8 frames / 80 ms)
    }
}

static float runClassifier(WakeWord::Impl& d) {
    if (d.emb_size < kClsWindowEmb) return -1.0f;

    std::vector<float> feat(static_cast<size_t>(kClsWindowEmb * kEmbDim));
    const int start = d.emb_size - kClsWindowEmb;
    for (int i = 0; i < kClsWindowEmb; ++i) {
        const auto& e = d.embAt(start + i);
        std::copy(e.begin(), e.end(), feat.begin() + i * kEmbDim);
    }

    std::array<int64_t, 3> shape{1, kClsWindowEmb, kEmbDim};
    Ort::Value in = Ort::Value::CreateTensor<float>(
        d.mem, feat.data(), feat.size(), shape.data(), shape.size());
    const char* in_names[]  = { d.cls_in.c_str() };
    const char* out_names[] = { d.cls_out.c_str() };
    auto out = d.cls->Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 1);
    return out[0].GetTensorData<float>()[0];
}

bool WakeWord::process(const float* frame, int n, float* score) {
    ++process_calls_;

    if (!ready_ && next_retry_ms_ > 0 && nowMs() >= next_retry_ms_) {
        next_retry_ms_ = 0;
        const int saved = reload_attempts_;
        if (!tryReload()) {
            reload_attempts_ = saved + 1;
            if (reload_attempts_ >= kMaxReloads) {
                if (notice_fn_)
                    notice_fn_("error", "audio.wakeword",
                               "Wake-word detector failed after 3 reload attempts");
            } else {
                const int delay = kBackoffSec[std::min(reload_attempts_ - 1, kMaxReloads - 1)];
                next_retry_ms_ = nowMs() + int64_t(delay) * 1000;
            }
        } else {
            reload_attempts_ = saved;
        }
    }

    if (!ready_ || frame == nullptr || n <= 0) return false;
    if (refractory_left_ > 0) --refractory_left_;

    // Accumulate into fixed buffer (no vector::erase).
    int off = 0;
    try {
        while (off < n) {
            const int space = kMelHopSamples - d_->audio_fill;
            const int take  = std::min(space, n - off);
            std::memcpy(d_->audio_accum + d_->audio_fill, frame + off,
                        static_cast<size_t>(take) * sizeof(float));
            d_->audio_fill += take;
            off += take;

            if (d_->audio_fill >= kMelHopSamples) {
                runMel(*d_, d_->audio_accum, kMelHopSamples);
                d_->audio_fill = 0;
            }
        }
        runEmbeddings(*d_);
        float s = runClassifier(*d_);
        if (score) *score = s < 0 ? 0.0f : s;

        reload_attempts_ = 0;
        next_retry_ms_ = 0;

        if (s >= threshold_ && refractory_left_ == 0) {
            refractory_left_ = kRefractoryFrames;
            PM_INFO("audio.wakeword: '{}' detected (score {:.2f})", phrase_, s);
            return true;
        }
    } catch (const Ort::Exception& e) {
        onInferenceError(e.what());
    }
    return false;
}

} // namespace polymath::audio
