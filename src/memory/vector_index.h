#pragma once
//
// VectorIndex — thin, thread-safe wrapper over hnswlib for cosine semantic
// recall.  Owns one HNSW graph persisted under Paths::vectors().  Labels are the
// `memories.vector_id` values (we use the memory row id as the hnsw label), so a
// search result maps straight back to a memories row.
//
// Embeddings are produced elsewhere (InferenceManager::embed); this class only
// stores/searches the resulting float vectors.  Cosine similarity is obtained by
// L2-normalizing every vector and using hnswlib's inner-product space (for unit
// vectors, inner product == cosine).  hnswlib returns a "distance" of
// (1 - inner_product); we convert that back to a cosine score in [-1, 1].
//
// Pinned against hnswlib v0.8.0 (header-only INTERFACE target `hnswlib`).
//
#include "types.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

// hnswlib is header-only; we forward-declare to keep it out of this header and
// avoid leaking its (warning-heavy) headers into every translation unit.
namespace hnswlib {
template <typename T> class HierarchicalNSW;
template <typename T> class SpaceInterface;
}

namespace polymath {

// One semantic-search result: the stored label (== memories.id) and the cosine
// similarity score (higher is more similar; 1.0 == identical direction).
struct VectorHit {
    int64_t label = 0;
    float   score = 0.0f;
};

class VectorIndex {
public:
    VectorIndex();
    ~VectorIndex();

    VectorIndex(const VectorIndex&) = delete;
    VectorIndex& operator=(const VectorIndex&) = delete;

    // Open (or create) the on-disk index for vectors of dimension `dim`. The
    // index files live in `dir` (typically Paths::vectors()). If an index file
    // exists but its dimension differs from `dim`, it is discarded and rebuilt
    // empty (the embedding model changed). `max_elements` is the initial graph
    // capacity; the index auto-grows past it. Returns false on hard failure.
    bool open(const std::filesystem::path& dir, int dim, size_t max_elements = 4096);

    bool isOpen() const;
    int  dim() const { return dim_; }
    size_t size() const;   // number of live labels

    // Insert (or replace) a vector under `label`. `vec` must have dim() elems;
    // it is L2-normalized internally. Returns false if the index is closed, the
    // dimension mismatches, or the vector is all-zero (cannot be normalized).
    bool add(int64_t label, const Embedding& vec);

    // Remove a label from search results (lazy delete — the slot is reused on a
    // re-add of the same label). No-op if the label is unknown.
    void remove(int64_t label);

    // k-NN search. Returns up to `k` hits sorted by descending cosine score.
    // Empty if the index is closed/empty or `vec` is invalid.
    std::vector<VectorHit> search(const Embedding& vec, int k) const;

    // Persist the graph to disk (called after batches of adds and on shutdown).
    // Cheap no-op when nothing changed since the last save.
    bool save();

private:
    std::filesystem::path indexPath() const;   // dir_/index.hnsw
    std::filesystem::path metaPath()  const;   // dir_/index.meta (records dim)

    int  readSavedDim() const;                 // dim recorded in metaPath(), or -1
    void writeMeta() const;                     // (re)write the meta sidecar

    // Normalize `in` into `out` (len dim_). Returns false if ||in|| ~= 0.
    bool normalize(const Embedding& in, std::vector<float>& out) const;

    void growIfNeeded(size_t want);   // resize the graph to fit `want` elements

    mutable std::mutex mtx_;          // guards every hnswlib call (not internally MT-safe for writes)
    std::filesystem::path dir_;
    int    dim_   = 0;
    bool   dirty_ = false;            // unsaved changes since last save()

    std::unique_ptr<hnswlib::SpaceInterface<float>>     space_;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>>    index_;

    // HNSW build/query params (sane defaults for a few-thousand-item personal
    // memory; tunable later via settings if needed).
    static constexpr size_t kM              = 16;    // graph connectivity
    static constexpr size_t kEfConstruction = 200;   // build-time accuracy
    static constexpr size_t kEfSearch       = 64;    // query-time accuracy
};

} // namespace polymath
