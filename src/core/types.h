#pragma once
//
// Hearth core value types.  These are plain, dependency-light structs shared
// across every service.  Keep them POD-ish and trivially copyable/movable so
// they can be passed through the EventBus (queued Qt connections) safely.
//
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <map>

namespace polymath {

using Clock     = std::chrono::system_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Bytes     = std::vector<uint8_t>;
using Embedding = std::vector<float>;

// --- Inference -------------------------------------------------------------

enum class ModelRole { Fast, Heavy, Vision, Embedding };

struct ModelSpec {
    std::string id;            // registry id (stable key)
    std::string display_name;
    std::string path;          // path to .gguf
    ModelRole   role = ModelRole::Fast;
    int         n_ctx = 8192;
    int         n_gpu_layers = 999;   // 999 = offload all; lowered by VRAM budget
    std::string chat_template; // empty = use model's built-in template
    std::string mmproj_path;   // CLIP/mmproj projector for VLM roles
    float       default_temp = 0.7f;
};

enum class Role { System, User, Assistant, Tool };

struct ChatMessage {
    Role        role = Role::User;
    std::string content;
    std::string name;          // tool name (for Role::Tool) or speaker
    Bytes       image;         // optional inline image (VLM); empty otherwise
};

struct SamplingParams {
    float temperature = 0.7f;
    float top_p = 0.9f;
    int   top_k = 40;
    float repeat_penalty = 1.1f;
    int   max_tokens = 1024;
    std::string grammar;       // optional GBNF (e.g. for constrained tool JSON)
};

struct ChatRequest {
    std::string              model_id;   // "" = active fast model
    std::vector<ChatMessage> messages;
    SamplingParams           sampling;
    std::vector<std::string> tool_names; // tools offered this turn (allow-list)
    std::string              request_id;  // correlates streamed tokens/results
};

// --- Vision ----------------------------------------------------------------

struct Frame {
    int       camera_id = -1;
    int       width = 0;
    int       height = 0;
    Bytes     jpeg;            // encoded JPEG (cheap to pass / store as thumbnail)
    TimePoint ts;
};

struct BoundingBox { float x, y, w, h, score; std::string label; };

struct Detection {
    int                      camera_id = -1;
    std::vector<BoundingBox> boxes;
    std::optional<int64_t>   user_id;   // resolved face identity, if any
    TimePoint                ts;
};

// --- Audio -----------------------------------------------------------------

struct Utterance {
    std::string            text;
    std::optional<int64_t> speaker_id;
    bool                   is_ambient = false;   // continuous vs. post-wakeword
    float                  confidence = 0.0f;
    std::string            source;               // "" = local mic; else satellite/room id
    TimePoint              ts;
};

// --- Generic ---------------------------------------------------------------

inline int64_t to_unix(TimePoint t) {
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}

} // namespace polymath

// Qt metatype registration for the value types passed as queued-signal payloads.
// Declared here, at the type definitions, so every consumer sees them before any
// Q_OBJECT slot/signal references them (prevents QMetaTypeId double-instantiation
// when a header declares e.g. a slot taking const Utterance&).
#include <QMetaType>
Q_DECLARE_METATYPE(polymath::Utterance)
Q_DECLARE_METATYPE(polymath::Frame)
Q_DECLARE_METATYPE(polymath::Detection)
