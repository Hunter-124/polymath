#include "asr_whisper.h"
#include "audio_common.h"
#include "logging.h"

#include <algorithm>
#include <cmath>
#include <mutex>

// whisper.cpp is gated by POLYMATH_HAVE_WHISPER (set by CMake when the
// third_party/whisper.cpp submodule is present). Without it the module still
// compiles; transcribe() returns empty so the rest of the pipeline can be wired.
//
// Pinned to whisper.cpp post-1.6 (the GGML-backed API with whisper.h providing
// whisper_init_from_file_with_params / whisper_full). If building against an
// older release, whisper_init_from_file (no params) is the fallback.
#if defined(POLYMATH_HAVE_WHISPER)
#  include "whisper.h"
#  include <ggml-backend.h>   // ggml_backend_load_all() — register runtime backend DLLs
#endif

namespace polymath::audio {

struct AsrWhisper::Impl {
#if defined(POLYMATH_HAVE_WHISPER)
    whisper_context* ctx = nullptr;
#endif
};

AsrWhisper::AsrWhisper() : d_(std::make_unique<Impl>()) {}

AsrWhisper::~AsrWhisper() {
#if defined(POLYMATH_HAVE_WHISPER)
    if (d_->ctx) whisper_free(d_->ctx);
#endif
}

bool AsrWhisper::load(const std::filesystem::path& model_path, Mode mode) {
    mode_  = mode;
    ready_ = false;
    if (!std::filesystem::exists(model_path)) {
        PM_WARN("audio.asr: whisper model missing: {} ({} disabled)",
                model_path.string(), mode == Mode::Ambient ? "ambient" : "command");
        return false;
    }
#if defined(POLYMATH_HAVE_WHISPER)
    // A GGML_BACKEND_DL build ships ggml's backends as separate DLLs that must be
    // registered before ANY ggml engine (llama OR whisper) is used. The inference
    // module also does this, but ASR can initialise first — and the audio test links
    // no inference module — so ensure it once here. Idempotent; a harmless no-op when
    // the backends are statically linked.
    static std::once_flag s_ggmlBackends;
    std::call_once(s_ggmlBackends, [] { ggml_backend_load_all(); });

    whisper_context_params cparams = whisper_context_default_params();
    // GPU offload helps the larger command model; ambient/tiny is fine on CPU.
    cparams.use_gpu = (mode == Mode::Command);
    d_->ctx = whisper_init_from_file_with_params(model_path.string().c_str(), cparams);
    if (!d_->ctx) {
        PM_ERROR("audio.asr: whisper_init failed for {}", model_path.string());
        return false;
    }
    ready_ = true;
    PM_INFO("audio.asr: loaded {} model '{}'",
            mode == Mode::Ambient ? "ambient" : "command", model_path.filename().string());
    return true;
#else
    PM_WARN("audio.asr: built without whisper.cpp; '{}' is a no-op", model_path.string());
    return false;
#endif
}

std::string AsrWhisper::transcribe(const std::vector<float>& pcm, float* confidence) {
    if (confidence) *confidence = 0.0f;
    if (!ready_ || pcm.empty()) return {};

#if defined(POLYMATH_HAVE_WHISPER)
    // Whisper needs at least ~1 s of audio; pad very short segments with silence.
    std::vector<float> samples = pcm;
    const size_t kMin = static_cast<size_t>(kSampleRate);  // 1 s
    if (samples.size() < kMin) samples.resize(kMin, 0.0f);

    whisper_full_params wparams =
        whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress   = false;
    wparams.print_special    = false;
    wparams.print_realtime   = false;
    wparams.print_timestamps = false;
    wparams.translate        = false;
    wparams.single_segment   = (mode_ == Mode::Command); // commands are one phrase
    wparams.no_context       = true;
    wparams.suppress_blank   = true;
    wparams.language         = language_.c_str();
    wparams.n_threads        = threads_;
    wparams.temperature      = 0.0f;
    // Command mode can afford a small beam for accuracy; ambient stays greedy.
    if (mode_ == Mode::Command) {
        wparams.strategy           = WHISPER_SAMPLING_BEAM_SEARCH;
        wparams.beam_search.beam_size = 5;
    }

    if (whisper_full(d_->ctx, wparams, samples.data(), static_cast<int>(samples.size())) != 0) {
        PM_ERROR("audio.asr: whisper_full failed");
        return {};
    }

    std::string text;
    double sum_logprob = 0.0;
    int    token_count = 0;
    const int n_seg = whisper_full_n_segments(d_->ctx);
    for (int i = 0; i < n_seg; ++i) {
        text += whisper_full_get_segment_text(d_->ctx, i);
        const int n_tok = whisper_full_n_tokens(d_->ctx, i);
        for (int t = 0; t < n_tok; ++t) {
            auto td = whisper_full_get_token_data(d_->ctx, i, t);
            sum_logprob += td.plog;
            ++token_count;
        }
    }

    // Trim leading/trailing whitespace whisper inserts.
    auto not_space = [](unsigned char c){ return !std::isspace(c); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());

    if (confidence && token_count > 0) {
        // Map mean log-prob to a rough [0,1] confidence.
        const double mean = sum_logprob / token_count;
        *confidence = static_cast<float>(std::exp(mean));
    }
    return text;
#else
    (void)pcm;
    return {};
#endif
}

} // namespace polymath::audio
