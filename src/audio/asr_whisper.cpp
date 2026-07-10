#include "asr_whisper.h"
#include "audio_common.h"
#include "logging.h"

#include <algorithm>
#include <cmath>
#include <cctype>

// whisper.cpp is gated by POLYMATH_HAVE_WHISPER (set by CMake when the
// third_party/whisper.cpp submodule is present). Without it the module still
// compiles; transcribe() returns empty so the rest of the pipeline can be wired.
#if defined(POLYMATH_HAVE_WHISPER)
#  include "whisper.h"
#endif

namespace polymath::audio {

struct AsrWhisper::Impl {
#if defined(POLYMATH_HAVE_WHISPER)
    whisper_context* ctx = nullptr;
#endif
};

AsrWhisper::AsrWhisper() : d_(std::make_unique<Impl>()) {}

AsrWhisper::~AsrWhisper() {
    unload();
}

bool AsrWhisper::load(const std::filesystem::path& model_path, Mode mode) {
    // Already loaded this path — keep the resident context.
#if defined(POLYMATH_HAVE_WHISPER)
    if (ready_ && d_->ctx && model_path_ == model_path && mode_ == mode) {
        return true;
    }
    // Path/mode change: free any previous context first.
    if (d_->ctx) {
        whisper_free(d_->ctx);
        d_->ctx = nullptr;
        ready_ = false;
        ++unload_count_;
    }
#else
    if (ready_ && model_path_ == model_path && mode_ == mode) return true;
#endif

    mode_ = mode;
    model_path_ = model_path;
    ready_ = false;
    if (!std::filesystem::exists(model_path)) {
        PM_WARN("audio.asr: whisper model missing: {} ({} disabled)",
                model_path.string(), mode == Mode::Ambient ? "ambient" : "command");
        return false;
    }
#if defined(POLYMATH_HAVE_WHISPER)
    whisper_context_params cparams = whisper_context_default_params();
    // GPU offload helps the larger command model; ambient/tiny is fine on CPU.
    cparams.use_gpu = (mode == Mode::Command);
    d_->ctx = whisper_init_from_file_with_params(model_path.string().c_str(), cparams);
    if (!d_->ctx) {
        PM_ERROR("audio.asr: whisper_init failed for {}", model_path.string());
        return false;
    }
    ready_ = true;
    ++load_count_;
    PM_INFO("audio.asr: loaded {} model '{}' (load #{})",
            mode == Mode::Ambient ? "ambient" : "command",
            model_path.filename().string(), load_count_);
    return true;
#else
    PM_WARN("audio.asr: built without whisper.cpp; '{}' is a no-op", model_path.string());
    return false;
#endif
}

void AsrWhisper::unload() {
#if defined(POLYMATH_HAVE_WHISPER)
    if (d_->ctx) {
        whisper_free(d_->ctx);
        d_->ctx = nullptr;
        ++unload_count_;
        PM_INFO("audio.asr: unloaded {} model (unload #{})",
                mode_ == Mode::Ambient ? "ambient" : "command", unload_count_);
    }
#endif
    ready_ = false;
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
    wparams.single_segment   = (mode_ == Mode::Command);
    wparams.no_context       = true;
    wparams.suppress_blank   = true;
    wparams.language         = language_.c_str();
    wparams.n_threads        = threads_;
    wparams.temperature      = 0.0f;
    if (mode_ == Mode::Command) {
        wparams.strategy              = WHISPER_SAMPLING_BEAM_SEARCH;
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

    auto not_space = [](unsigned char c){ return !std::isspace(c); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());

    if (confidence && token_count > 0) {
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
