# hearth_mqtt — MQTT over umqtt.simple speaking FABRIC.md topics/payloads.
# birth/LWT (§2), announce (§3), event (§4), cmd/* dispatch (§7).
from umqtt.simple import MQTTClient
import ujson

class Mqtt:
    def __init__(self, host, port, device_id, kind, name, fw):
        self.device_id = device_id
        self.kind = kind
        self.name = name
        self.fw = fw
        self.base = "hearth/%s/" % device_id
        self.location = ""
        self.on_command = None
        self._announce = None
        will = (self.base + "status").encode()
        will_msg = ujson.dumps({"device_id": device_id, "kind": kind,
                                "online": False, "ts": 0}).encode()
        self.cli = MQTTClient(device_id, host, port=port, keepalive=20)
        self.cli.set_last_will(will, will_msg, retain=True, qos=0)
        self.cli.set_callback(self._cb)

    def _cb(self, topic, msg):
        t = topic.decode()
        i = t.find("/cmd/")
        if i >= 0 and self.on_command:
            self.on_command(t[i + 5:], msg.decode())

    def connect(self):
        self.cli.connect()
        self._birth()
        self.cli.subscribe((self.base + "cmd/#").encode())
        if self._announce:
            self.publish_announce(*self._announce)
        print("[mqtt] connected", self.device_id)

    def loop(self):
        try:
            self.cli.check_msg()
        except OSError:
            try: self.connect()
            except Exception as e: print("[mqtt] reconnect failed", e)

    def _birth(self):
        self._pub("status", {"device_id": self.device_id, "kind": self.kind,
                             "name": self.name, "online": True,
                             "fw": self.fw, "ts": 0}, retain=True)

    def _pub(self, suffix, obj, retain=False):
        self.cli.publish((self.base + suffix).encode(), ujson.dumps(obj).encode(), retain=retain)

    def publish_announce(self, endpoint, caps, transport="mqtt", instruments=None):
        self._announce = (endpoint, caps, transport, instruments)
        obj = {"device_id": self.device_id, "kind": self.kind, "name": self.name,
               "location": self.location, "fw": self.fw, "endpoint": endpoint,
               "transport": transport, "capabilities": caps}
        if instruments is not None:
            obj["instruments"] = instruments
        self._pub("announce", obj)

    def publish_event(self, ev_kind, confidence, thumb_b64, clip_url, ts):
        self._pub("event", {"device_id": self.device_id, "kind": ev_kind,
                            "confidence": round(confidence, 2),
                            "thumb_b64": thumb_b64, "clip_url": clip_url, "ts": ts})
