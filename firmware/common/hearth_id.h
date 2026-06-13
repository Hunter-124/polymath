#pragma once
// hearth_id — stable device identity derived from the Wi-Fi MAC.
// device_id = "hearth-<kind3>-<hex6>"  (FABRIC.md §1), e.g. "hearth-cam-a1b2c3".
// kind3 is the 3-char short tag; the long `kind` string goes in announce/presence.

#include <Arduino.h>

namespace hearth {

// Long `kind` values per FABRIC.md §1.
enum class Kind { Camera, VoiceSat, Instrument, Panel };

// 3-char tag used inside device_id (cam/mic/hmm/pnl). Distinct from the long kind.
const char* kindTag(Kind k);        // "cam" | "mic" | "hmm" | "pnl"
const char* kindString(Kind k);     // "camera" | "voice_sat" | "instrument" | "panel"

// Low 6 hex chars of the MAC, e.g. "a1b2c3". Stable across boots.
String macHex6();

// Full device_id: "hearth-<tag>-<hex6>". Computed once, cached.
String deviceId(Kind k);

} // namespace hearth
