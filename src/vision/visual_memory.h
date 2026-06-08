#pragma once
//
// VisualMemory — a small, thread-safe rolling buffer of recent JPEG frames and
// detections, one ring per camera.  Camera workers push the frames they emit;
// the Finder reads a snapshot of "recent + live" frames to answer last-seen
// object queries via the VLM.  Bounded by count and age so memory stays flat.
//
#include "types.h"

#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace polymath {

// One remembered observation: the encoded frame plus any boxes detected on it.
struct VisualSnapshot {
    Frame                    frame;
    std::vector<BoundingBox> boxes;     // person/object boxes, if any ran
    std::optional<int64_t>   user_id;   // resolved identity on this frame, if any
};

class VisualMemory {
public:
    explicit VisualMemory(size_t per_camera = 90,
                          std::chrono::seconds max_age = std::chrono::seconds(300));

    // Record a frame (and its detections) for a camera. Thread-safe; called from
    // camera worker threads. Old / overflowing entries are evicted.
    void push(const Frame& frame, const std::vector<BoundingBox>& boxes = {},
              std::optional<int64_t> user_id = std::nullopt);

    // The most recent frame for a camera (empty Frame if none). Thread-safe.
    Frame latest(int camera_id) const;

    // A snapshot of recent frames across all cameras, newest first, capped at
    // `limit` total. Used by the Finder to scan recent history.
    std::vector<VisualSnapshot> recent(size_t limit) const;

    // Camera ids that currently have at least one buffered frame.
    std::vector<int> cameras() const;

    void clear(int camera_id);          // e.g. when a camera is disabled
    void clearAll();

private:
    void evictLocked(std::deque<VisualSnapshot>& ring) const;

    mutable std::mutex mtx_;
    std::unordered_map<int, std::deque<VisualSnapshot>> rings_;
    size_t                    per_camera_;
    std::chrono::seconds      max_age_;
};

} // namespace polymath
