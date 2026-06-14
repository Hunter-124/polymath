# hearth_net — STA connect + SoftAP provisioning fallback (FABRIC.md §6).
# Persists creds to /sdcard/hearth/wifi.json. CanMV exposes the same network.WLAN
# API as MicroPython, with an AP_IF for the captive portal.
import network, ujson, time, socket

_CRED_PATH = "/sdcard/hearth/wifi.json"

def _load():
    try:
        with open(_CRED_PATH) as f:
            d = ujson.load(f)
            return d.get("ssid", ""), d.get("pass", "")
    except OSError:
        return "", ""

def save_creds(ssid, pw):
    try: import os; os.mkdir("/sdcard/hearth")
    except OSError: pass
    with open(_CRED_PATH, "w") as f:
        ujson.dump({"ssid": ssid, "pass": pw}, f)

class Net:
    def __init__(self):
        self.mode = "ap"          # "sta" | "ap"
        self.ip = "0.0.0.0"
        self.ap_ssid = ""

    def connect(self, hex6, fallback_ssid="", fallback_pass="", timeout_ms=20000):
        ssid, pw = _load()
        if not ssid:
            ssid, pw = fallback_ssid, fallback_pass
        if ssid:
            wlan = network.WLAN(network.STA_IF)
            wlan.active(True)
            wlan.connect(ssid, pw)
            t0 = time.ticks_ms()
            while not wlan.isconnected() and time.ticks_diff(time.ticks_ms(), t0) < timeout_ms:
                time.sleep_ms(250)
            if wlan.isconnected():
                self.mode = "sta"
                self.ip = wlan.ifconfig()[0]
                print("[net] STA up", self.ip)
                return True
        self._start_ap(hex6)
        return False

    def _start_ap(self, hex6):
        self.ap_ssid = "Hearth-Setup-" + hex6
        ap = network.WLAN(network.AP_IF)
        ap.active(True)
        ap.config(essid=self.ap_ssid)
        self.ip = ap.ifconfig()[0]
        self.mode = "ap"
        print("[net] provisioning AP", self.ap_ssid, "http://%s/" % self.ip)

    def serve_provisioning(self):
        # Tiny blocking captive server: serves a form and accepts POST /provision.
        s = socket.socket(); s.bind(("0.0.0.0", 80)); s.listen(1)
        page = (b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n"
                b"<h3>Hearth Setup</h3><form method=POST action=/provision>"
                b"SSID<br><input name=ssid><br>Pass<br><input name=pass type=password>"
                b"<br><br><button>Join</button></form>")
        while True:
            cl, _ = s.accept()
            req = cl.recv(2048)
            if b"POST /provision" in req:
                body = req.split(b"\r\n\r\n", 1)[-1].decode()
                kv = {}
                for pair in body.split("&"):
                    if "=" in pair:
                        k, v = pair.split("=", 1); kv[k] = v.replace("+", " ")
                if kv.get("ssid"):
                    cl.send(b"HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n{\"ok\":true}")
                    cl.close()
                    save_creds(kv["ssid"], kv.get("pass", ""))
                    time.sleep(1)
                    import machine; machine.reset()
                else:
                    cl.send(b"HTTP/1.0 400 Bad Request\r\n\r\n")
            else:
                cl.send(page)
            cl.close()
