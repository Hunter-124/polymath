#include "vram_budget.h"
#include "logging.h"

#include <algorithm>
#include <filesystem>
#include <numeric>

#ifdef POLYMATH_USE_CUDA
#  include <cuda_runtime.h>
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
} // namespace

VramBudget::VramBudget(size_t budgetMiB) : budget_mib_(budgetMiB) {
#ifdef POLYMATH_USE_CUDA
    size_t freeB = 0, totalB = 0;
    cudaError_t err = cudaMemGetInfo(&freeB, &totalB);
    if (err == cudaSuccess && totalB > 0) {
        cuda_available_    = true;
        fallback_total_mib_ = totalB / kMiB;
        // Never let the configured budget exceed the physical device total.
        budget_mib_ = std::min<size_t>(budget_mib_, fallback_total_mib_);
        PM_INFO("VramBudget: CUDA device {} MiB total, {} MiB free, budget {} MiB",
                totalB / kMiB, freeB / kMiB, budget_mib_);
    } else {
        PM_WARN("VramBudget: cudaMemGetInfo failed ({}); using fallback budget",
                static_cast<int>(err));
    }
#else
    PM_INFO("VramBudget: built without CUDA; using conservative {} MiB budget",
            budget_mib_);
#endif
}

VramBudget::DeviceMemory VramBudget::query() const {
#ifdef POLYMATH_USE_CUDA
    if (cuda_available_) {
        size_t freeB = 0, totalB = 0;
        if (cudaMemGetInfo(&freeB, &totalB) == cudaSuccess) {
            return {freeB / kMiB, totalB / kMiB};
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

int VramBudget::planGpuLayers(size_t modelMiB, int n_layers_total,
                              size_t reserveMiB) const {
    if (n_layers_total <= 0) return 0;
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
