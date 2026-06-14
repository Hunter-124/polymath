#include "event_bus.h"
#include <QMetaType>

namespace polymath {

EventBus& EventBus::instance() {
    static EventBus bus;   // Meyers singleton; lives on the main (GUI) thread.
    return bus;
}

EventBus::EventBus() {
    // Register every payload so queued (cross-thread) connections can copy them.
    qRegisterMetaType<polymath::Utterance>("polymath::Utterance");
    qRegisterMetaType<polymath::Frame>("polymath::Frame");
    qRegisterMetaType<polymath::Detection>("polymath::Detection");
    qRegisterMetaType<polymath::WakeWordEvent>("polymath::WakeWordEvent");
    qRegisterMetaType<polymath::SpeakRequest>("polymath::SpeakRequest");
    qRegisterMetaType<polymath::TokenChunk>("polymath::TokenChunk");
    qRegisterMetaType<polymath::ToolCallEvent>("polymath::ToolCallEvent");
    qRegisterMetaType<polymath::ToolResultEvent>("polymath::ToolResultEvent");
    qRegisterMetaType<polymath::FindObjectResult>("polymath::FindObjectResult");
    qRegisterMetaType<polymath::TaskEvent>("polymath::TaskEvent");
    qRegisterMetaType<polymath::ReminderFired>("polymath::ReminderFired");
    qRegisterMetaType<polymath::PrivacyChanged>("polymath::PrivacyChanged");
    qRegisterMetaType<polymath::Notice>("polymath::Notice");
    qRegisterMetaType<polymath::InstrumentReading>("polymath::InstrumentReading");
    qRegisterMetaType<polymath::DevicePresence>("polymath::DevicePresence");
    qRegisterMetaType<polymath::LabStepEvent>("polymath::LabStepEvent");
}

} // namespace polymath
