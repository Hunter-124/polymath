#include "visual_memory.h"

#include <algorithm>
#include <utility>

namespace polymath {

VisualMemory::VisualMemory(size_t per_camera, std::chrono::seconds max_age)
    : per_camera_(per_camera), max_age_(max_age) {}

void VisualMemory::evictLocked(std::deque<VisualSnapshot>& ring) const {
    while (ring.size() > per_camera_)
        ring.pop_front();
    const auto cutoff = Clock::now() - max_age_;
    while (!ring.empty() && ring.front().frame.ts < cutoff)
        ring.pop_front();
}

void VisualMemory::push(const Frame& frame, const std::vector<BoundingBox>& boxes,
                        std::optional<int64_t> user_id) {
    if (frame.jpeg.empty())
        return;
    std::lock_guard lk(mtx_);
    auto& ring = rings_[frame.camera_id];
    ring.push_back(VisualSnapshot{frame, boxes, user_id});
    evictLocked(ring);
}

Frame VisualMemory::latest(int camera_id) const {
    std::lock_guard lk(mtx_);
    auto it = rings_.find(camera_id);
    if (it == rings_.end() || it->second.empty())
        return Frame{};
    return it->second.back().frame;
}

std::vector<VisualSnapshot> VisualMemory::recent(size_t limit) const {
    std::lock_guard lk(mtx_);
    // Gather lightweight (timestamp, pointer) refs first so we only deep-copy the
    // top `limit` JPEG-bearing snapshots, not the whole buffer.
    std::vector<std::pair<TimePoint, const VisualSnapshot*>> refs;
    for (const auto& [cam, ring] : rings_)
        for (const auto& s : ring)
            refs.emplace_back(s.frame.ts, &s);

    std::sort(refs.begin(), refs.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });   // newest first
    if (refs.size() > limit)
        refs.resize(limit);

    std::vector<VisualSnapshot> out;
    out.reserve(refs.size());
    for (const auto& [ts, ptr] : refs)
        out.push_back(*ptr);   // copy under lock; pointers stay valid until unlock
    return out;
}

std::vector<int> VisualMemory::cameras() const {
    std::lock_guard lk(mtx_);
    std::vector<int> ids;
    ids.reserve(rings_.size());
    for (const auto& [cam, ring] : rings_)
        if (!ring.empty())
            ids.push_back(cam);
    return ids;
}

void VisualMemory::clear(int camera_id) {
    std::lock_guard lk(mtx_);
    rings_.erase(camera_id);
}

void VisualMemory::clearAll() {
    std::lock_guard lk(mtx_);
    rings_.clear();
}

} // namespace polymath
