# cam-k230 (Pro cam, Canaan CanMV-K230) — config. Copy to config.py.

DEVICE_NAME = "Driveway"
LOCATION    = "Exterior"

MQTT_HOST = "hearth-hub.local"
MQTT_PORT = 1883

# Conservative person threshold; live-tunable via cmd/config + POST /config.
PERSON_THRESHOLD = 0.60
RETENTION_DAYS   = 14

# KPU YOLO person model on the SD card (CanMV .kmodel). If missing, the firmware
# falls back to a frame-difference motion gate (events become kind="motion").
MODEL_PATH = "/sdcard/models/person_yolov8n.kmodel"

# Wi-Fi: CanMV provisions creds at first run via the SoftAP captive flow in
# hearth_net.py; these are only a fallback if you prefer to hard-set them.
WIFI_SSID = ""
WIFI_PASS = ""
