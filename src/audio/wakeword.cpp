#include "wakeword.h"
#include "audio_common.h"
#include "onnx_util.h"
#include "logging.h"

#include <algorithm>
#include <array>

// openWakeWord ONNX front-end. Tensor shapes / window sizes follow the upstream
// openWakeWord 0.6.x model exports:
//   melspectrogram.onnx  in  [1, samples]            out [1,1,frames,32]
//   embedding_model.onnx in  [1,76,32,1]             out [1,1,1,96]
//   <wakeword>.onnx      in  [1,16,96]               out [1,1]
//
// The mel output is scaled to roughly match the training pipeline
// (mel/10 + 2), per the upstream implementation.

namespace polymath::audio {

namespace {
constexpr int   kMelBins        = 32;
constexpr int   kEmbWindowMels  = 76;   // mel frames per embedding
constexpr int   kEmbDim         = 96;
constexpr int   kClsWindowEmb   = 16;   // embeddings per classifier inference
constexpr int   kMelHopSamples  = 1280; // 80 ms produces ~8 mel frames
constexpr int   kRefractoryFrames = 12; // ~1 s suppression after a hit
}

struct WakeWord::Impl {
    std::unique_ptr<Ort::Session> mel;
    std::unique_ptr<Ort::Session> emb;
    std::unique_ptr<Ort::Session> cls;
    Ort::MemoryInfo mem{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};

    // I/O names retrieved at load time (kept alive for inference calls).
    Ort::AllocatedStringPtr mel_in, mel_out, emb_in, emb_out, cls_in, cls_out;

    // Rolling feature buffers.
    std::deque<std::array<float, kMelBins>> mel_frames;   // accumulated mel frames
    std::deque<std::array<float, kEmbDim>>  emb_frames;   // accumulated embeddings
    std::vector<float>                      audio_accum;  // leftover raw samples
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
    phrase_ = phrase;
    ready_  = false;

    d_->mel = loadSession(model_dir / "melspectrogram.onnx");
    d_->emb = loadSession(model_dir / "embedding_model.onnx");
    d_->cls = loadSession(model_dir / (phrase + ".onnx"));
    if (!d_->mel || !d_->emb || !d_->cls) {
        PM_WARN("audio.wakeword: detector disabled (missing models for '{}')", phrase);
        return false;
    }

    Ort::AllocatorWithDefaultOptions alloc;
    d_->mel_in  = d_->mel->GetInputNameAllocated(0, alloc);
    d_->mel_out = d_->mel->GetOutputNameAllocated(0, alloc);
    d_->emb_in  = d_->emb->GetInputNameAllocated(0, alloc);
    d_->emb_out = d_->emb->GetOutputNameAllocated(0, alloc);
    d_->cls_in  = d_->cls->GetInputNameAllocated(0, alloc);
    d_->cls_out = d_->cls->GetOutputNameAllocated(0, alloc);

    reset();
    ready_ = true;
    PM_INFO("audio.wakeword: loaded '{}' (threshold {:.2f})", phrase, threshold_);
    return true;
}

void WakeWord::reset() {
    d_->mel_frames.clear();
    d_->emb_frames.clear();
    d_->audio_accum.clear();
    refractory_left_ = 0;
}

// Runs the melspectrogram model on a block of raw samples, appending mel frames.
static void runMel(WakeWord::Impl& d, const float* samples, int n) {
    std::array<int64_t, 2> shape{1, n};
    Ort::Value in = Ort::Value::CreateTensor<float>(
        d.mem, const_cast<float*>(samples), n, shape.data(), shape.size());
    const char* in_names[]  = { d.mel_in.get() };
    const char* out_names[] = { d.mel_out.get() };
    auto out = d.mel->Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 1);

    auto info = out[0].GetTensorTypeAndShapeInfo();
    auto dims = info.GetShape();                 // [1,1,frames,32]
    const float* data = out[0].GetTensorData<float>();
    const int frames = static_cast<int>(dims[dims.size() - 2]);
    for (int f = 0; f < frames; ++f) {
        std::array<float, kMelBins> row;
        for (int b = 0; b < kMelBins; ++b)
            row[b] = data[f * kMelBins + b] / 10.0f + 2.0f;  // upstream scaling
        d.mel_frames.push_back(row);
    }
}

// Consumes complete 76-mel windows to produce 96-dim embeddings.
static void runEmbeddings(WakeWord::Impl& d) {
    while (static_cast<int>(d.mel_frames.size()) >= kEmbWindowMels) {
        std::vector<float> win(kEmbWindowMels * kMelBins);
        for (int f = 0; f < kEmbWindowMels; ++f)
            std::copy(d.mel_frames[f].begin(), d.mel_frames[f].end(),
                      win.begin() + f * kMelBins);

        std::array<int64_t, 4> shape{1, kEmbWindowMels, kMelBins, 1};
        Ort::Value in = Ort::Value::CreateTensor<float>(
            d.mem, win.data(), win.size(), shape.data(), shape.size());
        const char* in_names[]  = { d.emb_in.get() };
        const char* out_names[] = { d.emb_out.get() };
        auto out = d.emb->Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 1);

        const float* edata = out[0].GetTensorData<float>();
        std::array<float, kEmbDim> e;
        std::copy(edata, edata + kEmbDim, e.begin());
        d.emb_frames.push_back(e);

        // Hop by 8 mel frames (matches oWW: 8 frames per 80 ms step).
        for (int i = 0; i < 8 && !d.mel_frames.empty(); ++i)
            d.mel_frames.pop_front();
    }
}

// Runs the classifier over the latest 16 embeddings. Returns score or -1.
static float runClassifier(WakeWord::Impl& d) {
    if (static_cast<int>(d.emb_frames.size()) < kClsWindowEmb) return -1.0f;

    std::vector<float> feat(kClsWindowEmb * kEmbDim);
    const int start = static_cast<int>(d.emb_frames.size()) - kClsWindowEmb;
    for (int i = 0; i < kClsWindowEmb; ++i)
        std::copy(d.emb_frames[start + i].begin(), d.emb_frames[start + i].end(),
                  feat.begin() + i * kEmbDim);

    std::array<int64_t, 3> shape{1, kClsWindowEmb, kEmbDim};
    Ort::Value in = Ort::Value::CreateTensor<float>(
        d.mem, feat.data(), feat.size(), shape.data(), shape.size());
    const char* in_names[]  = { d.cls_in.get() };
    const char* out_names[] = { d.cls_out.get() };
    auto out = d.cls->Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 1);

    // Bound unbounded embedding growth (keep ~2 s of history).
    while (d.emb_frames.size() > 32) d.emb_frames.pop_front();
    return out[0].GetTensorData<float>()[0];
}

bool WakeWord::process(const float* frame, int n, float* score) {
    if (!ready_ || frame == nullptr || n <= 0) return false;
    if (refractory_left_ > 0) { --refractory_left_; }

    // Accumulate; run mel on whole 1280-sample hops.
    d_->audio_accum.insert(d_->audio_accum.end(), frame, frame + n);
    try {
        while (static_cast<int>(d_->audio_accum.size()) >= kMelHopSamples) {
            runMel(*d_, d_->audio_accum.data(), kMelHopSamples);
            d_->audio_accum.erase(d_->audio_accum.begin(),
                                  d_->audio_accum.begin() + kMelHopSamples);
        }
        runEmbeddings(*d_);
        float s = runClassifier(*d_);
        if (score) *score = s < 0 ? 0.0f : s;

        if (s >= threshold_ && refractory_left_ == 0) {
            refractory_left_ = kRefractoryFrames;
            PM_INFO("audio.wakeword: '{}' detected (score {:.2f})", phrase_, s);
            return true;
        }
    } catch (const Ort::Exception& e) {
        PM_ERROR("audio.wakeword: inference error: {}", e.what());
        ready_ = false;
    }
    return false;
}

} // namespace polymath::audio
