#pragma once
//
// EventBus — the single thread-safe message hub for Polymath.
//
// CONTRACT (read me):
//   * Every backend service lives on its own QThread and NEVER calls another
//     service directly.  All cross-service communication goes through this bus.
//   * Connect to a signal to receive; call the matching emit-helper to publish.
//     Because emitters and receivers live on different threads, Qt delivers via
//     queued connections automatically — payloads are copied, so they must be
//     trivially copyable value types (see types.h) and Q_DECLARE_METATYPE'd.
//   * There is one process-wide instance: EventBus::instance().
//
#include "types.h"
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace polymath {

// Payloads that need richer shape than types.h provides.
struct WakeWordEvent     { QString phrase; int64_t ts; };
// append=true: enqueue more speech without cutting current playback (streaming TTS).
// flush=true: wait until the playback queue drains (end of streamed reply).
struct SpeakRequest {
    QString text;
    QString voice;
    QString request_id;
    bool append = false;
    bool flush  = false;
};
struct TokenChunk        { QString request_id; QString text; bool done; };
struct ToolCallEvent     { QString request_id; QString tool; QString args_json; };
struct ToolResultEvent   { QString request_id; QString tool; QString result_json; bool ok; };
struct TaskEvent         { int64_t task_id; QString type; QString status; QString detail; };
struct ReminderFired     { int64_t reminder_id; QString text; };
struct FindObjectResult  { QString query; QString answer; int camera_id; int64_t ts; };
struct PrivacyChanged    { QString key; bool enabled; };
struct Notice            { QString level; QString source; QString message; };

// --- Overhaul payloads (A2): surfaces, goals, external agent sessions ----------
// action: spawn|close|arrange|open_page
// type:   placeholder|image|web|video|monitor
// A3: extended with optional spawn_surface hints (caption/md/x/y/w/h/group).
// All new fields default to "empty" (blank string, -1 sentinel for
// x/y/w/h meaning "no hint given") so existing spawn/close/arrange/open_page
// producers that only set id/action/type/title/args_json are unaffected.
struct SurfaceRequest {
    QString id;
    QString action;
    QString type;
    QString title;
    QString args_json;
    // A3 additions (all optional):
    QString caption;        // subtitle shown under the title (e.g. image caption)
    QString md;             // markdown body, for a future text/note card
    double  x = -1;         // position/size hints; -1 = unset, layout engine decides
    double  y = -1;
    double  w = -1;
    double  h = -1;
    QString group;          // research-board grouping key (surfaces sharing a
                             // group render together under one labeled frame)
};

// A3: `ui_control open_page` publishes this instead of a SurfaceRequest.
// page is a canonical, fixed nav-rail page id (lowercase snake_case — see
// kPageAliases in uicontrol_tool.cpp for the full alias table and
// docs/overhaul2/results/A3_notes.md for the authoritative id list).
// AppController relays it as navigateRequested(QString page); the QML handler
// (nav rail routing) lands in E4 — this node only publishes/relays.
struct NavigateRequest {
    QString page;
    QString args_json;   // reserved passthrough for page-specific args (usually "{}")
};

// A3: `ui_control window` publishes this. verb is one of:
// present|fullscreen|restore|always_on_top|normal|raise|hide_to_tray.
// AppController relays it as windowRequested(QString verb); the QML window
// handlers land in E4 — this node only publishes/relays.
struct WindowRequest {
    QString verb;
};
// Goal terminal/progress update for NotificationsModel + chat delivery.
struct GoalUpdate {
    QString goal_id;
    QString title;
    QString status;    // running|done|failed|cancelled
    QString summary;
};
// Normalized external-agent session event (Claude Code / Codex / PTY).
struct AgentSessionEvent {
    QString session_id;
    QString kind;      // Started|Thinking|ToolUse|AssistantText|NeedsInput|...
    QString text;
    QString raw_json;
    double  cost_usd = 0;
    qint64  ts = 0;
};

class EventBus : public QObject {
    Q_OBJECT
public:
    static EventBus& instance();

    // Emit helpers (call from any thread).
    void publishWakeWord(const WakeWordEvent& e)        { emit wakeWord(e); }
    void publishUtterance(const Utterance& u)           { emit utterance(u); }
    void publishSpeak(const SpeakRequest& s)            { emit speakRequested(s); }
    void publishToken(const TokenChunk& t)              { emit tokenStreamed(t); }
    void publishToolCall(const ToolCallEvent& c)        { emit toolCalled(c); }
    void publishToolResult(const ToolResultEvent& r)    { emit toolResulted(r); }
    void publishFrame(const Frame& f)                   { emit frameReady(f); }
    void publishDetection(const Detection& d)           { emit detection(d); }
    void publishFindObject(const FindObjectResult& r)   { emit findObjectDone(r); }
    void publishTask(const TaskEvent& t)                { emit taskUpdated(t); }
    void publishReminder(const ReminderFired& r)        { emit reminderFired(r); }
    void publishPrivacy(const PrivacyChanged& p)        { emit privacyChanged(p); }
    void publishNotice(const Notice& n)                 { emit notice(n); }
    void publishSurfaceRequest(const SurfaceRequest& r) { emit surfaceRequested(r); }
    void publishGoalUpdate(const GoalUpdate& g)         { emit goalUpdated(g); }
    void publishAgentSessionEvent(const AgentSessionEvent& e) { emit agentSessionEvent(e); }
    void publishNavigateRequest(const NavigateRequest& n) { emit navigateRequested(n); }
    void publishWindowRequest(const WindowRequest& w)   { emit windowRequested(w); }

signals:
    // --- audio ---
    void wakeWord(const polymath::WakeWordEvent&);
    void utterance(const polymath::Utterance&);        // ASR result (ambient or command)
    void speakRequested(const polymath::SpeakRequest&); // -> TTS
    // --- inference / agent ---
    void tokenStreamed(const polymath::TokenChunk&);    // streaming LLM output
    void toolCalled(const polymath::ToolCallEvent&);
    void toolResulted(const polymath::ToolResultEvent&);
    // --- vision ---
    void frameReady(const polymath::Frame&);            // live tile for UI
    void detection(const polymath::Detection&);         // motion/person/face
    void findObjectDone(const polymath::FindObjectResult&);
    // --- scheduler / proactive ---
    void taskUpdated(const polymath::TaskEvent&);
    void reminderFired(const polymath::ReminderFired&);
    // --- system ---
    void privacyChanged(const polymath::PrivacyChanged&);
    void notice(const polymath::Notice&);               // log/toast surface
    // --- overhaul: surfaces / goals / external sessions ---
    void surfaceRequested(const polymath::SurfaceRequest&);
    void goalUpdated(const polymath::GoalUpdate&);
    void agentSessionEvent(const polymath::AgentSessionEvent&);
    // A3: ui_control open_page / window actions.
    void navigateRequested(const polymath::NavigateRequest&);
    void windowRequested(const polymath::WindowRequest&);

private:
    EventBus();
    Q_DISABLE_COPY(EventBus)
};

} // namespace polymath

// Utterance / Frame / Detection are Q_DECLARE_METATYPE'd in types.h (at their
// definitions). The event-only payloads below are declared here.
Q_DECLARE_METATYPE(polymath::WakeWordEvent)
Q_DECLARE_METATYPE(polymath::SpeakRequest)
Q_DECLARE_METATYPE(polymath::TokenChunk)
Q_DECLARE_METATYPE(polymath::ToolCallEvent)
Q_DECLARE_METATYPE(polymath::ToolResultEvent)
Q_DECLARE_METATYPE(polymath::FindObjectResult)
Q_DECLARE_METATYPE(polymath::TaskEvent)
Q_DECLARE_METATYPE(polymath::ReminderFired)
Q_DECLARE_METATYPE(polymath::PrivacyChanged)
Q_DECLARE_METATYPE(polymath::Notice)
Q_DECLARE_METATYPE(polymath::SurfaceRequest)
Q_DECLARE_METATYPE(polymath::GoalUpdate)
Q_DECLARE_METATYPE(polymath::AgentSessionEvent)
Q_DECLARE_METATYPE(polymath::NavigateRequest)
Q_DECLARE_METATYPE(polymath::WindowRequest)
