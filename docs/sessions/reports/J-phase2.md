# Wave 3 · Card J — Phase 2: ESP32-CAM real-world + browser automation — REPORT

**Status: PASS** (both halves demonstrated, not deferred). Build green, `ctest`
11/11 passing including the new `j_phase2` test.

Owns: `firmware/esp32cam/` + new `src/agent/tools/browser_drive.*`. Additive
registration touched `src/agent/tools/register_tools.cpp` and
`src/agent/CMakeLists.txt`. One count assertion in another card's test
(`tests/test_agent_e2e.cpp`, 16→17 tools) was updated — the direct, sanctioned
consequence of registering a new builtin tool (see "Files changed").

---

## 1) ESP32-CAM end-to-end (simulated, no board) — VERIFIED

**What was verified (automated, `tests/test_j_phase2_e2e.cpp` half A):**
An in-process `QTcpServer` streams `multipart/x-mixed-replace; boundary=frame`
JPEG frames in the **exact** framing the shipped `firmware/esp32cam/esp32cam.ino`
emits on `GET /stream` (`\r\n--frame\r\n` + `Content-Type: image/jpeg` +
`Content-Length` + JPEG bytes). A **real** `CameraWorker` (the production ingest
class, unmodified) is pointed at `http://127.0.0.1:<port>/stream` and the test
asserts the full path:

- `onlineChanged(true)` — camera comes online (OpenCV/FFMPEG MJPEG-over-HTTP).
- `EventBus::frameReady` — live tile frames decoded + published (≈10–14 in the run).
- `EventBus::detection` with a `motion` box — the MOG2 motion gate fired.
- an `events` row written for the camera — the durable record the agent tools read.
- **Auto-reconnect**: kill the server → worker logs "stream stalled; reconnecting"
  and goes `online=0`; restart a server on the same port → worker reconnects
  (`online=1`) and frame count resumes climbing.

Frames come from `tests/fixtures/vision/people_walking.avi` (the Card C clip) when
present, else a synthesised sliding-box clip (same fallback as `test_vision_e2e`).
The worker runs with `yolo=nullptr, faces=nullptr` on purpose — this exercises the
decode + motion-gate + event path model-free (exactly the state before YOLO loads;
the person/face stages are separately covered by `test_vision_e2e`).

Sample run (ctest):
```
[A] ESP32-CAM simulator -> camera_worker MJPEG ingest
    MJPEG source: 60 frames from people_walking.avi
    serving MJPEG at http://127.0.0.1:59695/stream
    online=1 frames=11 motionDetections=1
    events rows for cam 7: 1
    OK: ingest -> tile frames + motion Detection + events row
    testing auto-reconnect (kill stream -> offline -> restart -> online)
    [warning] CameraWorker[7] stream stalled; reconnecting
    after kill: online=0
    after restart on :59695  online=1 frames=14 (was 13 at reconnect, 13 before kill)
    OK: auto-reconnect verified (offline -> online, frames resume)
```

**Real-board path:** documented in `firmware/esp32cam/README.md` — AI-Thinker flash
steps (Arduino IDE board/partition/FTDI wiring, GPIO0→GND jumper, upload, Serial
Monitor stream URL) were already present; this card added an explicit
"End-to-end verification (real board, step by step)" section, a "Simulated path
(no board)" section explaining the framing the test reproduces, and a LAN-security
note. The firmware itself is complete and was not modified.

**Formally not exercised here:** flashing a physical AI-Thinker board (no board on
this machine). Everything the board would feed Polymath — the MJPEG wire format,
ingest, decode, motion, events, reconnect — IS exercised against a byte-identical
software stream, so the only unproven link is the OV2640→firmware capture, which
is firmware logic unchanged from what shipped.

---

## 2) browser_drive tool — VERIFIED (real Chrome round-trip)

New `ITool` `browser_drive` (`src/agent/tools/browser_drive.{h,cpp}`). It:

1. **Launches** Chrome/Chromium/Edge (auto-detected) with
   `--headless=new --remote-debugging-port=<p> --remote-allow-origins=* --user-data-dir=<temp>`.
   Chrome's stdout/stderr are sent to the null device (otherwise the unread pipe
   fills and Chrome deadlocks). A RAII guard always terminates Chrome + cleans the
   throwaway profile on exit.
2. **Discovers** the page target's WebSocket debugger URL via CDP's HTTP `/json`
   endpoint (reads the authoritative bound port from `<profile>/DevToolsActivePort`
   when available, normalises the ws authority to the port we actually reached).
3. **Connects** the CDP WebSocket and drives a scripted round-trip:
   `Page.enable` / `Runtime.enable` → `Page.navigate` → wait for
   `Page.loadEventFired` → optional `type_into`/`type_text` (sets `.value` + fires
   input/change) → optional `click` → `Runtime.evaluate` to extract `{title, url, text}`.
4. **Surfaces** to the activity log via `EventBus::publishNotice` (source `"browser"`)
   on launch and on extraction — the same path other tools use.

**Allow-listing:** registered as a normal builtin; `ToolRegistry::specs(allow)`
treats an empty personality allow-list as "all tools", and both shipped bundles
(`ada-lovelace`, `marcus-aurelius`) use `"tools": []`, so `browser_drive` is
offered to them automatically — identical to `fetch_page`/`web_search`. No
personality-module edits were needed.

**Verified (automated, `tests/test_j_phase2_e2e.cpp` half B):**
- **B1 (no Chrome needed):** the RFC6455 framing — `cdpws::encodeTextFrame` masks
  client frames (FIN+text opcode, mask bit set) and `cdpws::decodeFrame`
  reassembles the payload, handles the 16-bit extended-length form, and returns
  "incomplete" on a partial buffer (the receive-loop invariant).
- **B2 (live Chrome):** the real tool drives Chrome to a local `file://` page,
  types into an `#q` field, and extracts the title + body over CDP. Asserted
  `title == "Polymath CDP Test"` and the body contains the page text. If no Chrome
  is installed the tool returns a clean "Chrome not installed" and the test treats
  B2 as a documented skip.

Sample run (ctest, Chrome 149 present):
```
[B2] browser_drive live round-trip (Chrome DevTools Protocol)
    ok=1 summary="Drove browser to "Polymath CDP Test" (70 chars)"
    title="Polymath CDP Test"  text[0..40]="Hello from browser_drive..."
    OK: launched Chrome, navigated file://, typed into a field, extracted title + body via CDP
```

---

## Why a hand-rolled WebSocket (contract note)

**`Qt6::WebSockets` is NOT in the deployed Qt kit.** `scripts/build-cpu.ps1`
installs Qt 6.6.3 with only `qtmultimedia qtimageformats qtshadertools`; there is
no `Qt6WebSockets` cmake config or `Qt6WebSockets.lib` under
`C:\Qt\6.6.3\msvc2019_64`. Only `Qt6::Network` is available.

Per the card's guidance ("if Qt6::WebSockets is NOT in the kit, say so and use the
lightest viable approach rather than silently pulling a big new dependency"), the
CDP transport is a **minimal RFC6455 client implemented on `QTcpSocket`** (already
provided by `Qt6::Network`, which `pm_agent` already links). CDP's socket is plain
`ws://127.0.0.1:<port>/...` to localhost, so a masked-client / unmasked-server
text-frame implementation is sufficient — no fragmentation, no TLS, no permessage
extensions. This adds **zero** new third-party dependencies.

**Contract request (optional, non-blocking):** if a future card wants WSS or a
fuller WebSocket surface, add `qtwebsockets` to the aqtinstall module list in
`scripts/build-cpu.ps1` and swap the transport for `QWebSocket`. Logged in
`docs/sessions/contract-requests.md`. Not required for this card.

---

## Files changed (and why)

- **`firmware/esp32cam/README.md`** — added real-board step-by-step verification,
  a "Simulated path (no board)" section documenting the MJPEG framing the test
  reproduces + how to run `ctest -R j_phase2`, and a LAN-security note. (Firmware
  source unchanged — it was already complete.)
- **`src/agent/tools/browser_drive.h` / `.cpp`** (NEW) — the tool + the `cdpws`
  framing helpers (exposed in the header so the test can verify the wire format).
- **`src/agent/tools/register_tools.cpp`** — register `BrowserDriveTool` (the one
  sanctioned additive touch; +1 include, +1 `reg.add`).
- **`src/agent/CMakeLists.txt`** — add `tools/browser_drive.cpp` to `pm_agent`.
- **`tests/test_j_phase2_e2e.cpp`** (NEW) — both halves above.
- **`tests/CMakeLists.txt`** — append-only registration of `test_j_phase2_e2e`
  (links `pm_vision pm_agent pm_core Qt6::Network ${OpenCV_LIBS}`; offscreen QPA;
  240s timeout).
- **`tests/test_agent_e2e.cpp`** — bumped the registry-size assertion `16 → 17` and
  added `browser_drive` to the existence check. This is Card B's test, but the
  change is the unavoidable consequence of the sanctioned new registration; it is a
  single localized count update, no behavioural change.

No edits to FROZEN contracts (`event_bus.*`, `schema.h`), no edits to other `src/`
modules, no GUI edits.

---

## Environment notes / gotchas (for the next agent)

- **Build/run env:** the test needs Qt + OpenCV (incl. `opencv_videoio_ffmpeg*.dll`)
  + ONNX Runtime on PATH. `ctest` via `build-cpu.ps1` sets this; running the exe
  directly requires `…\Qt\6.6.3\msvc2019_64\bin`, the ORT `lib`, and OpenCV `bin`
  on PATH (+ `QT_QPA_PLATFORM=offscreen`).
- **Chrome quirks handled in the tool** (each cost a real debug cycle):
  - Chrome's DevTools HTTP server requires **HTTP/1.1** (an HTTP/1.0 request is
    closed with no body).
  - When the requested debug port differs from the bound one, Chrome reports the
    page `webSocketDebuggerUrl` **without a port**; the tool rebuilds the authority
    using the port it actually reached `/json` on.
  - Unread Chrome stdout/stderr pipes deadlock the process → redirected to null.
  - `--remote-allow-origins=*` is mandatory on modern Chrome for the WS handshake.
- **CDP transport is nested-loop-free:** all waits use synchronous
  `QTcpSocket::waitForReadyRead`, not a nested `QEventLoop`. A nested loop re-enters
  / stalls when `invoke()` runs under an already-spinning Qt loop (the agent worker
  thread, or a test pumping events) — this caused an early hang and was the reason
  for the switch.
- **`PM_BROWSER_DEBUG=1`** turns on step traces (launch / discover / ws-connect /
  navigate / extract) to stderr — left in (no-op unless set) for field debugging.

## Residual gaps (explicit)

- **Physical AI-Thinker board** not flashed/exercised here (no hardware). The
  ingest contract is proven against a byte-identical software stream; flashing
  steps are documented.
- **browser_drive form-fill** sets `.value` + dispatches input/change (works for
  standard inputs and most React-controlled inputs). It does not synthesise real
  key events via `Input.dispatchKeyEvent`; if a site needs per-keystroke handlers,
  that's a small follow-up using the same CDP session.
- **browser_drive under a restrictive sandbox:** if the host sandbox blocks a
  child process from binding a localhost port, discovery times out and the tool
  returns a clean error (the test skips that case). Verified working under both the
  normal and disabled sandbox on this box.
