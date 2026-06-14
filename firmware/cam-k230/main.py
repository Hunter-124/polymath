# cam-k230 — Hearth Pro cam (Canaan CanMV-K230, RISC-V + KPU).
# ----------------------------------------------------------------------------
# CanMV MicroPython: KPU YOLO person detection -> clip-on-person to microSD ->
# publish FABRIC.md §4 CameraEvent over MQTT, plus the device HTTP API (§6) and
# SoftAP provisioning. Conforms byte-for-byte to docs/FABRIC.md.
#
# This targets the CanMV image's `sensor` + `image` + KPU modules. On a host
# without those modules (e.g. CI lint) the imports are guarded so the file still
# parses; the detector + clip logic degrade to documented fallbacks.

import time, os, ubinascii, ujson

import config
from hearth_id import device_id, mac_hex6
from hearth_auth import Auth
from hearth_net import Net
from hearth_mqtt import Mqtt
from hearth_httpd import Httpd
from detector import PersonDetector

FW = "0.2.0"
KIND = "camera"
CLIPS_DIR = "/sdcard/clips"

# --- camera (CanMV sensor API) ----------------------------------------------
try:
    import sensor, image
    _HAVE_CAM = True
except Exception:
    _HAVE_CAM = False
    sensor = None; image = None

def cam_init():
    if not _HAVE_CAM:
        print("[cam] sensor module absent (host lint); camera disabled")
        return False
    sensor.reset()
    sensor.set_pixformat(sensor.RGB565)
    sensor.set_framesize(sensor.VGA)        # 640x480 preview/clip
    sensor.skip_frames(time=300)
    return True

def grab():
    return sensor.snapshot() if _HAVE_CAM else None

def now_unix():
    t = time.time()
    return int(t) if t > 1700000000 else 0

# --- clip recording ----------------------------------------------------------
def ensure_dir(p):
    try: os.mkdir(p)
    except OSError: pass

def record_clip(ts, frames=30):
    # K230 has a real CPU; we still write an MJPEG-style container ("<ts>.mjpeg")
    # to keep parity with the ESP tiers. A future build can mux real MP4 via the
    # CanMV media encoder. The clip_url is opaque to the hub.
    ensure_dir(CLIPS_DIR)
    fname = "%d.mjpeg" % ts
    thumb_b64 = ""
    try:
        f = open(CLIPS_DIR + "/" + fname, "wb")
        for i in range(frames):
            img = grab()
            if img is None: break
            jpg = img.compress(quality=70)
            data = jpg.bytearray() if hasattr(jpg, "bytearray") else bytes(jpg)
            if i == 0:
                thumb_b64 = ubinascii.b2a_base64(data).decode().strip()
            f.write(b"\r\n--hearthframe\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n" % len(data))
            f.write(data)
        f.close()
    except OSError as e:
        print("[clip] write failed", e)
        return None, thumb_b64
    prune()
    return fname, thumb_b64

def prune():
    cutoff = now_unix() - config.RETENTION_DAYS * 86400 if now_unix() else 0
    try:
        for fn in os.listdir(CLIPS_DIR):
            try: ts = int(fn.split(".")[0])
            except ValueError: continue
            if cutoff and ts < cutoff:
                os.remove(CLIPS_DIR + "/" + fn)
    except OSError:
        pass

# --- wiring ------------------------------------------------------------------
def main():
    auth = Auth()
    net = Net()
    hex6 = mac_hex6()
    if not net.connect(hex6, config.WIFI_SSID, config.WIFI_PASS):
        print("[boot] provisioning mode")
        net.serve_provisioning()            # blocks until provisioned + reboot
        return

    did = device_id()
    cam_ok = cam_init()
    detector = PersonDetector(config.MODEL_PATH)
    print("[boot] detector:", detector.name())

    cfg = {"person_threshold": config.PERSON_THRESHOLD,
           "retention_days": config.RETENTION_DAYS, "face": False}

    lan_host = did + ".local"
    http_base = "http://" + net.ip

    httpd = Httpd(did, KIND, config.DEVICE_NAME, FW, auth, CLIPS_DIR,
                  lan_host, net.ap_ssid, cfg,
                  snapshot_fn=lambda: (grab().compress(quality=70).bytearray() if cam_ok and grab() else None),
                  now_fn=now_unix)
    httpd.http_base(net.ip)
    httpd.start()

    mqtt = Mqtt(config.MQTT_HOST, config.MQTT_PORT, did, KIND, config.DEVICE_NAME, FW)
    mqtt.location = config.LOCATION
    def on_cmd(name, payload):
        if name == "config":
            try: cfg.update(ujson.loads(payload))
            except Exception: pass
        elif name == "ota":
            print("[ota] requested:", payload)   # TODO(ota): fetch+verify+flash on K230
    mqtt.on_command = on_cmd
    try:
        mqtt.connect()
    except Exception as e:
        print("[mqtt] connect failed", e)

    level = "reliable" if detector.ready else "trigger"
    caps = {"stream": False, "snapshot": True, "clips": True,
            "person_detect": level, "resolution": "640x480", "sd": True}
    try:
        mqtt.publish_announce(http_base, caps)
    except Exception:
        pass

    last_infer = 0
    while True:
        mqtt.loop()
        img = grab()
        if img is not None and time.ticks_diff(time.ticks_ms(), last_infer) > 200:
            last_infer = time.ticks_ms()
            is_person, conf = detector.detect(img)
            ts = now_unix() or int(time.time())
            if detector.ready and is_person and conf >= cfg["person_threshold"]:
                fname, thumb = record_clip(ts)
                url = (http_base + "/clips/" + fname) if fname else ""
                mqtt.publish_event("person", conf, thumb, url, ts)
                time.sleep(4)
            elif (not detector.ready) and conf >= 0.04:
                fname, thumb = record_clip(ts)
                url = (http_base + "/clips/" + fname) if fname else ""
                mqtt.publish_event("motion", conf, thumb, url, ts)
                time.sleep(4)
        time.sleep_ms(20)

main()
