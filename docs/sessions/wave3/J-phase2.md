# Wave 3 · Card J — Phase 2: ESP32-CAM real-world + browser automation

**Read [`../SHARED.md`](../SHARED.md) first.**  Owns: **`firmware/esp32cam/`** + a new
**`src/agent/tools/browser_drive.*`** tool (the tool is additive; coordinate its registration
with the agent owner). These are the two explicitly-deferred Phase-2 items.

## Goal
Close the deferred Phase-2 work — or formally cut it.

## Do
1. **ESP32-CAM end-to-end** — flash the shipped firmware to an AI-Thinker board, point it at the
   app, and verify a live camera tile + motion/person events from real MJPEG/RTSP, with
   auto-reconnect. **No board?** Stand up a software MJPEG server from a recorded clip, verify the
   ingest → tile → event path that way, and document the real-board flashing steps.
2. **browser_drive tool** — the Phase-2 web-automation tool: drive Chrome via the DevTools
   Protocol (CDP websocket from C++) for clicking / forms / logged-in sites; allow-listed per
   personality; surfaced in the activity log. Register it as a new `ITool`.

## How
- Read `firmware/esp32cam/*` + its README, `src/vision/camera_worker.*` (ingest/reconnect), and
  `src/agent/i_tool.h` + `tool_registry.*` for the tool pattern.

## Done when
A live (or simulated) ESP32 stream shows a tile + a person event, and `browser_drive` performs a
scripted navigate + extract round-trip. Report at `docs/sessions/reports/J-phase2.md`.
