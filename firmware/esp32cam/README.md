# ESP32-CAM firmware (Polymath)

Turns an **AI-Thinker ESP32-CAM** into an MJPEG camera that Polymath consumes.

## What you need
- AI-Thinker ESP32-CAM module (the common one with an OV2640 + PSRAM).
- A USB-to-TTL (FTDI) adapter, **or** an ESP32-CAM-MB programmer shield.
- Arduino IDE with the **esp32** boards package installed
  (Boards Manager → "esp32 by Espressif").

## Configure
Edit [`config.h`](config.h): set `WIFI_SSID`, `WIFI_PASS`, and a unique
`DEVICE_NAME` per camera (e.g. `polymath-cam1`, `polymath-cam2`).

## Flash (Arduino IDE)
1. **Tools → Board →** `AI Thinker ESP32-CAM`.
2. **Tools → Partition Scheme →** `Huge APP (3MB No OTA)`.
3. Wire the FTDI adapter: `5V→5V`, `GND→GND`, `U0T→RX`, `U0R→TX`.
   For flashing, jumper **GPIO0 → GND**, then press the RST button.
4. Select the COM port and click **Upload**.
5. When it says "done uploading", **remove the GPIO0↔GND jumper** and press RST.
6. Open Serial Monitor @ `115200` — it prints the stream URL, e.g.
   `http://192.168.1.42/stream` (and `http://polymath-cam1.local/stream`).

## Use in Polymath
In the app's **Cameras** settings, add a camera with the stream URL
(`http://<ip-or-mdns>/stream`). The VisionService auto-reconnects if the camera
reboots.

## End-to-end verification (real board, step by step)
1. Flash the firmware (above) and confirm the Serial Monitor prints the stream URL.
2. Open `http://<ip>/stream` in a browser — you should see a live MJPEG image.
3. In Polymath → Cameras, add the camera with `http://<ip>/stream`.
4. Walk in front of the camera. Expect, in order:
   - a **live tile** for the camera in the dashboard (decoded frames),
   - a **motion** event in the activity log (MOG2 motion gate), and
   - once the YOLO model is loaded, a **person** event (and a **face** event if
     face recognition is on and the person is enrolled).
5. Power-cycle the board: the tile should drop to "offline", then return to
   "online" within a few seconds (auto-reconnect with backoff).

## Simulated path (no board) — what CI / a dev box uses instead
You don't need hardware to exercise the full ingest path. A software MJPEG server
that streams a recorded clip in the **exact** framing this firmware emits
(`multipart/x-mixed-replace; boundary=frame`, each part
`Content-Type: image/jpeg` + `Content-Length` + JPEG bytes) is indistinguishable
to Polymath's camera ingest from a real board.

- The automated test `tests/test_j_phase2_e2e.cpp` stands up exactly such a server
  in-process (a `QTcpServer` replaying frames from `tests/fixtures/vision/people_walking.avi`)
  and drives a real `CameraWorker` against `http://127.0.0.1:<port>/stream`,
  asserting the online → tile-frame → motion-event → DB-row path **and**
  auto-reconnect (kill the stream → offline → restart → online). Run it with
  `ctest --test-dir build/cpu -C Release -R j_phase2`.
- To drive the actual app by hand, point a Cameras entry at any local MJPEG server
  (e.g. a small PowerShell/Python script, or VLC's "stream to HTTP" of a clip)
  serving the framing above, then follow steps 4–5 of the real-board flow.

## Endpoints
| Path | Purpose |
|------|---------|
| `/stream` | MJPEG (`multipart/x-mixed-replace`) — the URL to register |
| `/snapshot` | single JPEG frame |
| `/status` | JSON `{name, ip, rssi}` |

## Tips
- If the image is garbled or the board brown-outs, use a solid 5V/2A supply
  (the camera draws spikes Wi-Fi can't tolerate on weak USB power).
- Lower resolution / raise JPEG quality number in `esp32cam.ino` (`frame_size`,
  `jpeg_quality`) if your Wi-Fi is congested.
- The stream is plain HTTP MJPEG (no auth). Keep these cameras on a trusted LAN
  or VLAN; do not port-forward `/stream` to the internet.
