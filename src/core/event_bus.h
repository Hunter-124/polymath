#pragma once
//
// EventBus — the single thread-safe message hub for Hearth.
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
// target "" => the local speaker; else a satellite/room id to route TTS back to.
struct SpeakRequest      { QString text; QString voice; QString request_id; QString target; };
struct TokenChunk        { QString request_id; QString text; bool done; };
struct ToolCallEvent     { QString request_id; QString tool; QString args_json; };
struct ToolResultEvent   { QString request_id; QString tool; QString result_json; bool ok; };
struct TaskEvent         { int64_t task_id; QString type; QString status; QString detail; };
struct ReminderFired     { int64_t reminder_id; QString text; };
struct FindObjectResult  { QString query; QString answer; int camera_id; int64_t ts; };
struct PrivacyChanged    { QString key; bool enabled; };
struct Notice            { QString level; QString source; QString message; };

// --- device fabric (v2) ----------------------------------------------------
// A single timestamped reading from a networked lab instrument.
struct InstrumentReading { QString instrument_id; QString device_id; double value;
                           QString unit; QString device_class; bool in_range; int64_t ts; };
// A device coming online / going offline (MQTT birth/LWT or mDNS).
struct DevicePresence    { QString device_id; QString kind; QString name; bool online; int64_t ts; };
// Progress of the interactive lab-session agent, for live UI/mobile display.
struct LabStepEvent      { int64_t session_id; int step_no; QString prompt; QString status;
                           double measured_value; QString unit; bool verified; };

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
    void publishInstrumentReading(const InstrumentReading& r) { emit instrumentReading(r); }
    void publishDevicePresence(const DevicePresence& p) { emit devicePresence(p); }
    void publishLabStep(const LabStepEvent& s)          { emit labStep(s); }
    // The assistant is actively driving the mouse/keyboard (computer-use). Each
    // action republishes active=true with a short human description; the UI shows a
    // glowing border while active and an activity-log line. (bool+QString are
    // built-in metatypes — no Q_DECLARE_METATYPE needed.)
    void publishDesktopControl(bool active, const QString& action) { emit desktopControl(active, action); }

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
    // --- device fabric (v2) ---
    void instrumentReading(const polymath::InstrumentReading&);
    void devicePresence(const polymath::DevicePresence&);
    void labStep(const polymath::LabStepEvent&);
    // --- computer use (the assistant driving the desktop) ---
    void desktopControl(bool active, QString action);
    // --- system ---
    void privacyChanged(const polymath::PrivacyChanged&);
    void notice(const polymath::Notice&);               // log/toast surface

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
Q_DECLARE_METATYPE(polymath::InstrumentReading)
Q_DECLARE_METATYPE(polymath::DevicePresence)
Q_DECLARE_METATYPE(polymath::LabStepEvent)
