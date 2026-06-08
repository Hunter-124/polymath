#pragma once
//
// TurnCollector — bridges InferenceManager::generate() (async; streams onto
// EventBus::tokenStreamed) into a synchronous "issue request, block until the
// matching request_id finishes" call usable from the agent worker thread.
//
// Internal to src/agent (NOT part of the frozen public contract). Mirrors the
// scheduler's StreamCollector but lives in the agent module so the agent owns
// no cross-module internals. The bus delivers token chunks from the inference
// worker thread, so shared state is mutex-guarded and we connect with a
// DirectConnection (no event loop required on the agent thread).
//
#include "event_bus.h"
#include <QObject>
#include <condition_variable>
#include <mutex>
#include <string>

namespace polymath {

class InferenceManager;

class TurnCollector : public QObject {
    Q_OBJECT
public:
    explicit TurnCollector(QObject* parent = nullptr);

    // Issue `req` to `inf`, block until the chunk stream for req.request_id ends
    // (done=true) or `timeout_ms` elapses, and return the concatenated text.
    // `ok` (optional) is set false on timeout. Reusable serially.
    std::string run(InferenceManager& inf, const ChatRequest& req,
                    int timeout_ms = 120000, bool* ok = nullptr);

private:
    void onToken(const TokenChunk& chunk);

    std::mutex              mtx_;
    std::condition_variable cv_;
    std::string             active_request_id_;
    std::string             buffer_;
    bool                    done_ = false;
};

} // namespace polymath
