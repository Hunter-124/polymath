#include "vram_budget.h"
#include "logging.h"

#include <algorithm>
#include <filesystem>
#include <numeric>

// GPU memory is queried through ggml's backend device registry rather than
// cudart directly, so ONE binary behaves correctly whether the CUDA backend is
// statically linked, loaded at runtime as ggml-cuda.dll, or absent entirely.
#ifdef POLYMATH_HAVE_LLAMA
#  include <ggml-backend.h>
#endif

namespace polymath {

namespace {
constexpr size_t kMiB = 1024ull * 1024ull;

// Heuristic head-room kept free on the GPU for fragmentation, cuBLAS workspaces,
// the desktop compositor, etc. Subtracted from both the device budget and the
// measured free pool when deciding how aggressively to offload.
constexpr size_t kSafetyMarginMiB = 768;

// Rough per-1k-token KV-cache cost for a mid-size model with GQA, fp16 cache.
// Deliberately generous so we under-offload rather than OOM.
constexpr size_t kKvMiBPer1kCtx = 128;

#ifdef POLYMATH_HAVE_LLAMA
// The first discrete-GPU device ggml knows about (after backends are loaded),
// or null on a CPU-only machine. ggml-cuda contributes a GPU device only when a
// usable NVIDIA GPU + driver are present, so this is our runtime "is there a GPU"
// gate — no compile-time CUDA switch involved.
ggml_backend_dev_t firstGpuDevice() {
    const size_t n = ggml_backend_dev_count();
    for (size_t i = 0; i < n; ++i) {
        ggml_backend_dev_t d = ggml_backend_dev_get(i);
        if (ggml_backend_dev_type(d) == GGML_BACKEND_DEVICE_TYPE_GPU) return d;
    }
    return nullptr;
}
#endif
} // namespace

VramBudget::VramBudget(size_t budgetMiB, bool assumeGpuForTests) : budget_mib_(budgetMiB) {
#ifdef POLYMATH_HAVE_LLAMA
    // Register any runtime backend libraries (ggml-cpu / ggml-cuda DLLs sitting
    // next to the exe). A harmless no-op when backends are statically linked.
    ggml_backend_load_all();

    if (ggml_backend_dev_t gpu = firstGpuDevice()) {
        size_t freeB = 0, totalB = 0;
        ggml_backend_dev_memory(gpu, &freeB, &totalB);
        if (totalB > 0) {
            cuda_available_     = true;            // a usable GPU backend is present
            fallback_total_mib_ = totalB / kMiB;
            // Never let the configured budget exceed the physical device total.
            budget_mib_ = std::min<size_t>(budget_mib_, fallback_total_mib_);
            PM_INFO("VramBudget: GPU backend present — {} MiB total, {} MiB free, budget {} MiB",
                    totalB / kMiB, freeB / kMiB, budget_mib_);
        }
    }
    if (!cuda_available_ && assumeGpuForTests) {
        cuda_available_ = true;
        fallback_total_mib_ = budget_mib_;
        PM_INFO("VramBudget: assuming GPU backend for deterministic budget tests");
    }
    if (!cuda_available_)
        PM_INFO("VramBudget: no GPU backend detected; running on CPU with a {} MiB budget",
                budget_mib_);
#else
    if (assumeGpuForTests) cuda_available_ = true;
    PM_INFO("VramBudget: built without the llama/ggml backend; using {} MiB budget",
            budget_mib_);
#endif
}

VramBudget::DeviceMemory VramBudget::query() const {
#ifdef POLYMATH_HAVE_LLAMA
    if (cuda_available_) {
        if (ggml_backend_dev_t gpu = firstGpuDevice()) {
            size_t freeB = 0, totalB = 0;
            ggml_backend_dev_memory(gpu, &freeB, &totalB);
            if (totalB > 0) return {freeB / kMiB, totalB / kMiB};
        }
    }
#endif
    // CPU-only / query failed: report the budget as both free and total so the
    // planner behaves as if a fixed-size device were available.
    const size_t reserved = reservedMiB();
    const size_t free = reserved < fallback_total_mib_
                            ? fallback_total_mib_ - reserved : 0;
    return {free, fallback_total_mib_};
}

size_t VramBudget::estimateModelMiB(const std::string& path, int n_ctx) {
    std::error_code ec;
    size_t weightsMiB = 0;
    if (std::filesystem::exists(path, ec)) {
        const auto bytes = std::filesystem::file_size(path, ec);
        if (!ec) weightsMiB = static_cast<size_t>(bytes / kMiB);
    }
    if (weightsMiB == 0) {
        // Unknown file — assume a 7B Q4 footprint so we still budget for it.
        weightsMiB = 4500;
    }
    const size_t kvMiB =
        (static_cast<size_t>(std::max(n_ctx, 0)) * kKvMiBPer1kCtx) / 1024;
    // Compute/graph overhead scales loosely with model size; add ~8%.
    const size_t overhead = weightsMiB / 12;
    return weightsMiB + kvMiB + overhead;
}

size_t VramBudget::estimateGpuFootprintMiB(const std::string& path, int n_ctx,
                                           int gpu_layers, int total_layers) {
    if (gpu_layers <= 0) return 0;

    std::error_code ec;
    size_t weightsMiB = 0;
    if (std::filesystem::exists(path, ec)) {
        const auto bytes = std::filesystem::file_size(path, ec);
        if (!ec) weightsMiB = static_cast<size_t>(bytes / kMiB);
    }
    if (weightsMiB == 0) weightsMiB = 4500;

    const int layers = std::max(total_layers, 1);
    const double offload =
        std::clamp(static_cast<double>(gpu_layers) / static_cast<double>(layers),
                   0.0, 1.0);
    const size_t gpuWeights = static_cast<size_t>(static_cast<double>(weightsMiB) * offload);
    const size_t kvMiB =
        (static_cast<size_t>(std::max(n_ctx, 0)) * kKvMiBPer1kCtx) / 1024;
    const size_t overhead = gpuWeights / 8;
    return gpuWeights + kvMiB + overhead;
}

int VramBudget::planGpuLayers(size_t modelMiB, int n_layers_total,
                              size_t reserveMiB) const {
    if (n_layers_total <= 0) return 0;
    if (!cuda_available_) return 0;
    if (modelMiB == 0)       return n_layers_total;   // nothing to weigh

    const DeviceMemory dev = query();

    // Available headroom = min(remaining budget, live free VRAM) - safety.
    const size_t alreadyReserved = reservedMiB() + reserveMiB;
    const size_t budgetLeft =
        budget_mib_ > alreadyReserved ? budget_mib_ - alreadyReserved : 0;
    size_t avail = std::min<size_t>(budgetLeft, dev.freeMiB);
    avail = avail > kSafetyMarginMiB ? avail - kSafetyMarginMiB : 0;

    if (avail == 0) {
        PM_WARN("VramBudget: no headroom for model (~{} MiB); running on CPU",
                modelMiB);
        return 0;
    }
    if (avail >= modelMiB) return n_layers_total;     // fits fully

    // Partial offload: layers are the dominant per-layer cost. Approximate the
    // share of the model that fits and offload that fraction of layers.
    const double fraction = static_cast<double>(avail) /
                            static_cast<double>(modelMiB);
    int layers = static_cast<int>(fraction * n_layers_total);
    layers = std::clamp(layers, 0, n_layers_total);
    PM_INFO("VramBudget: partial offload {}/{} layers (avail {} MiB, model ~{} MiB)",
            layers, n_layers_total, avail, modelMiB);
    return layers;
}

void VramBudget::reserve(const std::string& backend_id, size_t miB) {
    std::lock_guard lk(mtx_);
    footprints_[backend_id] = miB;
}

void VramBudget::release(const std::string& backend_id) {
    std::lock_guard lk(mtx_);
    footprints_.erase(backend_id);
}

size_t VramBudget::reservedMiB() const {
    std::lock_guard lk(mtx_);
    return std::accumulate(footprints_.begin(), footprints_.end(), size_t{0},
                           [](size_t a, const auto& kv) { return a + kv.second; });
}

size_t VramBudget::reservedFor(const std::string& backend_id) const {
    std::lock_guard lk(mtx_);
    auto it = footprints_.find(backend_id);
    return it == footprints_.end() ? 0 : it->second;
}

} // namespace polymath
