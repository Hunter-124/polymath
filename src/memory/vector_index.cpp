#include "vector_index.h"

#include "logging.h"

// hnswlib v0.8.0 — header-only. hnswalg.h pulls in the HierarchicalNSW impl and
// the space definitions (InnerProductSpace / L2Space) live in space_ip.h.
#include <hnswlib/hnswlib.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <system_error>

namespace polymath {

namespace {

// Marker written next to the index so we can detect a dimension change (the
// embedding model was swapped) and rebuild rather than load a mismatched graph.
constexpr const char* kMetaMagic = "PMVEC1";

} // namespace

VectorIndex::VectorIndex() = default;

VectorIndex::~VectorIndex() {
    // Best-effort flush so we never lose freshly-added memories on shutdown.
    if (dirty_) {
        try {
            save();
        } catch (const std::exception& e) {
            PM_ERROR("VectorIndex: save on shutdown failed: {}", e.what());
        }
    }
}

std::filesystem::path VectorIndex::indexPath() const { return dir_ / "index.hnsw"; }
std::filesystem::path VectorIndex::metaPath()  const { return dir_ / "index.meta"; }

bool VectorIndex::isOpen() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return index_ != nullptr;
}

size_t VectorIndex::size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!index_) return 0;
    return index_->getCurrentElementCount() - index_->getDeletedCount();
}

bool VectorIndex::open(const std::filesystem::path& dir, int dim, size_t max_elements) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (dim <= 0) {
        PM_ERROR("VectorIndex: refusing to open with non-positive dim {}", dim);
        return false;
    }

    dir_ = dir;
    dim_ = dim;
    dirty_ = false;

    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) {
        PM_ERROR("VectorIndex: cannot create '{}': {}", dir_.string(), ec.message());
        return false;
    }

    // Inner-product space over unit vectors == cosine similarity.
    space_ = std::make_unique<hnswlib::InnerProductSpace>(static_cast<size_t>(dim_));

    const bool have_index = std::filesystem::exists(indexPath());
    const int  saved_dim  = readSavedDim();

    if (have_index && saved_dim == dim_) {
        try {
            index_ = std::make_unique<hnswlib::HierarchicalNSW<float>>(
                space_.get(), indexPath().string(),
                /*nmslib=*/false,
                /*max_elements=*/0,            // 0 = keep the capacity from disk
                /*allow_replace_deleted=*/true);
            index_->setEf(kEfSearch);
            PM_INFO("VectorIndex: loaded {} vectors (dim={}) from {}",
                    index_->getCurrentElementCount(), dim_, indexPath().string());
            return true;
        } catch (const std::exception& e) {
            PM_WARN("VectorIndex: failed to load '{}' ({}); rebuilding empty",
                    indexPath().string(), e.what());
            // fall through to fresh build
        }
    } else if (have_index) {
        PM_WARN("VectorIndex: on-disk dim {} != requested {} (embedding model "
                "changed?); rebuilding empty index", saved_dim, dim_);
    }

    try {
        index_ = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            space_.get(),
            /*max_elements=*/std::max<size_t>(max_elements, 16),
            /*M=*/kM,
            /*ef_construction=*/kEfConstruction,
            /*random_seed=*/100,
            /*allow_replace_deleted=*/true);
        index_->setEf(kEfSearch);
        writeMeta();
        PM_INFO("VectorIndex: created empty index (dim={}, capacity={}) at {}",
                dim_, std::max<size_t>(max_elements, 16), indexPath().string());
        return true;
    } catch (const std::exception& e) {
        PM_ERROR("VectorIndex: failed to create index: {}", e.what());
        index_.reset();
        return false;
    }
}

int VectorIndex::readSavedDim() const {
    // (mtx_ already held by caller)
    std::ifstream in(metaPath());
    if (!in) return -1;
    std::string magic;
    int d = -1;
    in >> magic >> d;
    if (!in || magic != kMetaMagic) return -1;
    return d;
}

void VectorIndex::writeMeta() const {
    // (mtx_ already held by caller)
    std::ofstream out(metaPath(), std::ios::trunc);
    if (out) out << kMetaMagic << ' ' << dim_ << '\n';
}

bool VectorIndex::normalize(const Embedding& in, std::vector<float>& out) const {
    if (static_cast<int>(in.size()) != dim_) return false;
    double sumsq = 0.0;
    for (float v : in) sumsq += static_cast<double>(v) * v;
    const double norm = std::sqrt(sumsq);
    if (norm < 1e-12) return false;        // all-zero / degenerate vector
    out.resize(in.size());
    const float inv = static_cast<float>(1.0 / norm);
    for (size_t i = 0; i < in.size(); ++i) out[i] = in[i] * inv;
    return true;
}

void VectorIndex::growIfNeeded(size_t want) {
    // (mtx_ already held by caller)
    if (!index_) return;
    if (want <= index_->getMaxElements()) return;
    size_t cap = std::max<size_t>(index_->getMaxElements() * 2, want);
    try {
        index_->resizeIndex(cap);
        PM_INFO("VectorIndex: grew capacity to {}", cap);
    } catch (const std::exception& e) {
        PM_ERROR("VectorIndex: resize to {} failed: {}", cap, e.what());
    }
}

bool VectorIndex::add(int64_t label, const Embedding& vec) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!index_) {
        PM_WARN("VectorIndex: add() on closed index (label {})", label);
        return false;
    }
    std::vector<float> unit;
    if (!normalize(vec, unit)) {
        PM_WARN("VectorIndex: skip add label {} (dim mismatch {}!={} or zero vector)",
                label, vec.size(), dim_);
        return false;
    }

    growIfNeeded(index_->getCurrentElementCount() + 1);

    try {
        // hnsw labels are size_t; memory ids are positive autoincrement -> fits.
        const auto lab = static_cast<hnswlib::labeltype>(label);
        // replace_deleted=true lets a re-added label reuse a tombstoned slot.
        index_->addPoint(unit.data(), lab, /*replace_deleted=*/true);
        dirty_ = true;
        return true;
    } catch (const std::exception& e) {
        PM_ERROR("VectorIndex: addPoint(label={}) failed: {}", label, e.what());
        return false;
    }
}

void VectorIndex::remove(int64_t label) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!index_) return;
    try {
        index_->markDelete(static_cast<hnswlib::labeltype>(label));
        dirty_ = true;
    } catch (const std::exception&) {
        // Unknown label -> hnswlib throws; treat as a no-op.
    }
}

std::vector<VectorHit> VectorIndex::search(const Embedding& vec, int k) const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<VectorHit> hits;
    if (!index_ || k <= 0) return hits;

    const size_t live = index_->getCurrentElementCount() - index_->getDeletedCount();
    if (live == 0) return hits;

    std::vector<float> unit;
    if (!normalize(vec, unit)) {
        PM_WARN("VectorIndex: search with invalid query vector (dim {}!={} or zero)",
                vec.size(), dim_);
        return hits;
    }

    const size_t want = std::min<size_t>(static_cast<size_t>(k), live);
    try {
        // Returns a max-heap of (distance, label) with distance = 1 - cosine.
        auto pq = index_->searchKnn(unit.data(), want);
        hits.reserve(pq.size());
        while (!pq.empty()) {
            const auto& [dist, lab] = pq.top();
            VectorHit h;
            h.label = static_cast<int64_t>(lab);
            h.score = 1.0f - dist;        // inner-product space distance -> cosine
            hits.push_back(h);
            pq.pop();
        }
    } catch (const std::exception& e) {
        PM_ERROR("VectorIndex: searchKnn failed: {}", e.what());
        return {};
    }

    // searchKnn yields nearest-last (max-heap pop order); sort best-first.
    std::sort(hits.begin(), hits.end(),
              [](const VectorHit& a, const VectorHit& b) { return a.score > b.score; });
    return hits;
}

bool VectorIndex::save() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!index_) return false;
    if (!dirty_) return true;
    try {
        index_->saveIndex(indexPath().string());
        writeMeta();
        dirty_ = false;
        PM_DEBUG("VectorIndex: saved {} vectors to {}",
                 index_->getCurrentElementCount(), indexPath().string());
        return true;
    } catch (const std::exception& e) {
        PM_ERROR("VectorIndex: saveIndex failed: {}", e.what());
        return false;
    }
}

} // namespace polymath
