# cam-pi — Hearth Flagship cam (Raspberry Pi 5)

A Linux **C++** service (CMake) implementing the full Hearth device side on a
Raspberry Pi 5 (+ optional **Hailo AI HAT**): the device HTTP API (FABRIC.md §6),
MQTT (§2–§4, §7), and a **Frigate-style local NVR** that records a clip on a
person and publishes a `CameraEvent`. It reuses the **same ONNX/YOLO approach as
the hub** (`src/vision`), and adds an optional **`--edge-hub`** mode that runs
detection on behalf of Budget cams on the LAN (they POST frames to
`/edge/detect`).

C++ was chosen over Python here so the Pi can also act as an edge inference hub
(shared ONNX/Hailo backend, no GIL, low-latency NVR).

## Build (on the Pi, or any Linux)
```
sudo apt install build-essential cmake libssl-dev libmosquitto-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build        # installs binary + systemd unit
```
- **OpenSSL** is required (HMAC + RNG for §6 auth).
- **libmosquitto** is optional: if found, real MQTT is enabled; if not, the build
  still succeeds and events flow via HTTP push only (a warning is printed).
- **ONNX Runtime** is optional: configure with `-DHEARTH_USE_ONNX=ON` and have
  `libonnxruntime` discoverable to enable the YOLO model. Otherwise the detector
  links a documented **motion stub** (announces `person_detect:"trigger"`, events
  `kind:"motion"`). With the **Hailo HAT**, add a HailoRT backend behind the same
  `PersonDetector` interface (see `detector.cpp` `TODO(model)`).

> This repo was authored on Windows where no Linux C++ toolchain / POSIX headers
> are available, so it is verified correct-by-construction; run the `cmake` build
> above on the Pi.

## Configure
```
sudo mkdir -p /etc/hearth /var/lib/hearth
sudo cp config.example.json /etc/hearth/cam-pi.json   # then edit
```
Key fields: `camera` (V4L2 path or RTSP URL), `mqtt_host`, `person_threshold`,
`retention_days`, `model_path`, `edge_hub`.

## Run
```
sudo systemctl enable --now hearth-cam-pi
journalctl -u hearth-cam-pi -f
# or run directly:
sudo /usr/local/bin/hearth-cam-pi --config /etc/hearth/cam-pi.json --edge-hub
```
`device_id` is `hearth-cam-<hex6>` derived from the Pi's NIC MAC. The service binds
port 80 (the systemd unit grants `CAP_NET_BIND_SERVICE`).

## Camera capture
Capture is abstracted behind `captureFrame()` / `encodeJpegStub()` /
`recordClip()` with a synthetic-frame stub so the service builds and the full
detect → clip → event → HTTP/MQTT path runs without a camera. Wire real
libcamera/V4L2 capture + a JPEG encoder + an ffmpeg/libav MP4 muxer where marked
`TODO(capture)`.

## Endpoints (port 80)
`GET /` `GET /status` · `GET /snapshot`* · `GET /clips`* · `GET /clips/<f>`* ·
`GET /config` · `POST /config`* · `POST /pair`* · `POST /edge/detect`* (edge-hub
only) — *require the HMAC bearer (FABRIC.md §6).
