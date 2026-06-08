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
