#pragma once
//
// VramBudget — tracks the GPU memory the inference pool is allowed to consume
// (~8 GB target) and decides how many transformer layers a model may offload so
// the resident set fits.
//
// It detects the GPU at RUNTIME through ggml's backend device registry (so the
// same binary uses the GPU when an ggml-cuda backend + NVIDIA device are present
// and falls back to a conservative fixed budget on CPU-only machines) — there is
// no compile-time CUDA switch in this class.
//
// All sizes are MiB. The class is cheap and lock-free for reads; bookkeeping of
// per-backend footprints is guarded by an internal mutex so it can be poked from
// the InferenceManager thread while VRAM is queried elsewhere.
//
#include "types.h"

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

namespace polymath {

class VramBudget {
public:
    // budgetMiB: the ceiling the inference pool may occupy. Defaults to ~8 GiB.
    explicit VramBudget(size_t budgetMiB = 8192);

    // Live device totals. On non-CUDA builds returns the conservative defaults.
    struct DeviceMemory { size_t freeMiB = 0; size_t totalMiB = 0; };
    DeviceMemory query() const;

    bool   cudaAvailable() const { return cuda_available_; }
    size_t budgetMiB() const { return budget_mib_; }

    // Estimate the on-GPU weight cost (MiB) of a .gguf at `path` from its file
    // size, plus a KV-cache estimate for n_ctx. Used when picking n_gpu_layers.
    static size_t estimateModelMiB(const std::string& path, int n_ctx);

    // Decide how many of `n_layers_total` to offload for a model of approximately
    // `modelMiB`, leaving room for everything currently reserved plus a safety
    // margin. Returns 0..n_layers_total (clamped). reserveMiB lets the caller
    // pre-account for a model it is about to add but has not reserve()'d yet.
    int planGpuLayers(size_t modelMiB, int n_layers_total,
                      size_t reserveMiB = 0) const;

    // Per-backend footprint bookkeeping (so eviction frees the right amount).
    void   reserve(const std::string& backend_id, size_t miB);
    void   release(const std::string& backend_id);
    size_t reservedMiB() const;               // sum of all tracked footprints
    size_t reservedFor(const std::string& backend_id) const;

private:
    size_t budget_mib_;
    bool   cuda_available_ = false;
    size_t fallback_total_mib_ = 8192;        // assumed device total w/o CUDA

    mutable std::mutex                            mtx_;
    std::unordered_map<std::string, size_t>       footprints_;
};

} // namespace polymath
