#pragma once
//
// Finder — open-vocabulary "where did I last see X" search over the visual
// memory.  Given a natural-language query ("my keys", "the red mug"), it asks
// the InferenceManager's Vision model (describeImage) about a handful of recent
// frames, newest first, and stops at the first frame the VLM says contains the
// object.  Produces a human-readable last-seen answer (camera + time).
//
// Runs synchronously on the VisionService worker thread (describeImage blocks);
// the service publishes the FindObjectResult on the EventBus.
//
#include "event_bus.h"

#include <string>

namespace polymath {

class InferenceManager;
class VisualMemory;
class Database;

class Finder {
public:
    Finder(InferenceManager& inf, VisualMemory& mem, Database& db);

    // Scans up to `max_frames` recent frames for `query`. camera_name() resolves
    // ids to friendly names from the cameras table for the answer string.
    FindObjectResult find(const std::string& query, size_t max_frames = 8);

    void setMaxFrames(size_t n) { default_max_frames_ = n; }

private:
    // Ask the VLM whether `query` is visible in one frame. Returns a non-empty
    // "where" phrase when found, empty string otherwise.
    std::string askFrame(const Frame& frame, const std::string& query);
    std::string cameraName(int camera_id);

    InferenceManager& inf_;
    VisualMemory&     mem_;
    Database&         db_;
    size_t            default_max_frames_ = 8;
};

} // namespace polymath
