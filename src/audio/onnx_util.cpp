#include "onnx_util.h"

namespace polymath::audio {

Ort::Env& ortEnv() {
    // Intentionally leaked: must outlive every Ort::Session in the process.
    static Ort::Env* env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "polymath-audio");
    return *env;
}

Ort::SessionOptions defaultSessionOptions() {
    Ort::SessionOptions opts;
    // These models are tiny; one intra-op thread keeps latency predictable and
    // avoids stealing cores from llama/whisper worker threads.
    opts.SetIntraOpNumThreads(1);
    opts.SetInterOpNumThreads(1);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    opts.SetExecutionMode(ORT_SEQUENTIAL);
    return opts;
}

#ifdef _WIN32
std::wstring ortPath(const std::filesystem::path& p) { return p.wstring(); }
#else
std::string  ortPath(const std::filesystem::path& p) { return p.string(); }
#endif

} // namespace polymath::audio
