#include "turn_collector.h"
#include "inference_manager.h"
#include "logging.h"

#include <chrono>

namespace polymath {

TurnCollector::TurnCollector(QObject* parent) : QObject(parent) {
    // DirectConnection: the slot runs in the inference worker's thread context
    // (the bus emits there). All touched state is guarded by mtx_; we wake the
    // waiting agent thread via cv_. No event loop is needed on either side.
    connect(&EventBus::instance(), &EventBus::tokenStreamed,
            this, &TurnCollector::onToken, Qt::DirectConnection);
}

void TurnCollector::onToken(const TokenChunk& chunk) {
    TokenHook hook_copy;
    std::string delta;
    bool is_done = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (active_request_id_.empty() ||
            chunk.request_id.toStdString() != active_request_id_)
            return;                                  // not the request we're awaiting
        delta = chunk.text.toStdString();
        buffer_ += delta;
        is_done = chunk.done;
        if (chunk.done) {
            done_ = true;
            cv_.notify_all();
        }
        hook_copy = hook_;
    }
    // Run hook outside the lock so TTS / bus work cannot deadlock the wait.
    if (hook_copy)
        hook_copy(delta, is_done);
}

std::string TurnCollector::run(InferenceManager& inf, const ChatRequest& req,
                               int timeout_ms, bool* ok, TokenHook hook) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        active_request_id_ = req.request_id;
        buffer_.clear();
        done_ = false;
        hook_ = std::move(hook);
    }

    inf.generate(req);   // async; tokens arrive on tokenStreamed (DirectConnection)

    std::unique_lock<std::mutex> lk(mtx_);
    const bool finished = cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                       [this] { return done_; });
    std::string result = buffer_;
    active_request_id_.clear();
    hook_ = {};
    if (ok) *ok = finished;
    if (!finished)
        PM_WARN("TurnCollector: timed out after {} ms for request '{}'",
                timeout_ms, req.request_id);
    return result;
}

} // namespace polymath
