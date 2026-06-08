#pragma once
//
// Small helpers around the ONNX Runtime C++ API shared by the wake-word and VAD
// front-ends.  Pinned to ONNX Runtime 1.17.x (onnxruntime_cxx_api.h).
//
// A single process-wide Ort::Env is created lazily; sessions are CPU-only by
// default (these models are tiny — sub-millisecond — and we keep the GPU free
// for llama.cpp/whisper). Switch to the CUDA EP here if profiling demands it.
//
#include <onnxruntime_cxx_api.h>
#include <filesystem>
#include <string>

namespace polymath::audio {

// Lazily-created, leaked-on-purpose singleton env (ORT requires it outlive all
// sessions; process-lifetime is correct here).
Ort::Env& ortEnv();

// Builds session options tuned for these small realtime models.
Ort::SessionOptions defaultSessionOptions();

// Converts a filesystem path to the platform-native ORT path string
// (wchar_t on Windows, char elsewhere).
#ifdef _WIN32
std::wstring ortPath(const std::filesystem::path& p);
#else
std::string ortPath(const std::filesystem::path& p);
#endif

} // namespace polymath::audio
