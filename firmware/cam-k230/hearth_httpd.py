# hearth_httpd — device HTTP API (FABRIC.md §6) for the K230, a tiny socket
# server run in a background thread. Endpoints: / /status /snapshot /clips
# /clips/<f> /config (GET/POST) /pair. Media + clips require the HMAC bearer.
import socket, ujson, _thread, os

class Httpd:
    def __init__(self, device_id, kind, name, fw, auth, clips_dir,
                 lan_host, softap, cfg, snapshot_fn, now_fn):
        self.device_id = device_id; self.kind = kind; self.name = name; self.fw = fw
        self.auth = auth; self.clips_dir = clips_dir
        self.lan_host = lan_host; self.softap = softap
        self.cfg = cfg                      # dict: person_threshold/retention_days/face
        self.snapshot_fn = snapshot_fn      # () -> jpeg bytes or None
        self.now_fn = now_fn                # () -> unix seconds

    def start(self):
        _thread.start_new_thread(self._serve, ())

    def http_base(self, ip):
        self._ip = ip
        return "http://" + ip

    def _authorized(self, headers, path):
        return self.auth.verify(headers.get("authorization", ""), path,
                                headers.get("x-hearth-ts", "0"), self.now_fn())

    def _serve(self):
        s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("0.0.0.0", 80)); s.listen(2)
        while True:
            try:
                cl, _ = s.accept()
                self._handle(cl)
            except Exception as e:
                print("[httpd]", e)

    def _handle(self, cl):
        req = cl.recv(2048).decode("utf-8", "ignore")
        if not req:
            cl.close(); return
        line = req.split("\r\n", 1)[0]
        parts = line.split(" ")
        method, path = (parts + ["", "/"])[:2]
        path = path.split("?", 1)[0]
        headers = {}
        for h in req.split("\r\n")[1:]:
            if ":" in h:
                k, v = h.split(":", 1); headers[k.strip().lower()] = v.strip()
        body = req.split("\r\n\r\n", 1)[-1]

        if path == "/" and method == "GET":
            qr = self.auth.qr_payload(self.device_id, self.kind, self.softap, self.lan_host)
            self._send(cl, 200, "text/html",
                       "<h3>Hearth %s — %s</h3><pre>%s</pre>" % (self.kind, self.name, qr))
        elif path == "/status" and method == "GET":
            self._json(cl, 200, {"device_id": self.device_id, "kind": self.kind,
                                 "name": self.name, "fw": self.fw,
                                 "lan_host": self.lan_host,
                                 "capabilities": {"stream": True, "snapshot": True,
                                                  "clips": True, "person_detect": "reliable",
                                                  "sd": True}})
        elif path == "/snapshot" and method == "GET":
            if not self._authorized(headers, path): return self._json(cl, 401, {"ok": False})
            jpg = self.snapshot_fn()
            if not jpg: return self._json(cl, 503, {"ok": False})
            self._send_bytes(cl, 200, "image/jpeg", jpg)
        elif path == "/clips" and method == "GET":
            if not self._authorized(headers, path): return self._json(cl, 401, {"ok": False})
            self._send(cl, 200, "application/json", self._clips_json())
        elif path.startswith("/clips/") and method == "GET":
            if not self._authorized(headers, path): return self._json(cl, 401, {"ok": False})
            self._send_file(cl, path[len("/clips/"):])
        elif path == "/config" and method == "GET":
            self._json(cl, 200, self.cfg)
        elif path == "/config" and method == "POST":
            if not self._authorized(headers, path): return self._json(cl, 401, {"ok": False})
            try: self.cfg.update(ujson.loads(body))
            except Exception: pass
            self._json(cl, 200, {"ok": True})
        elif path == "/pair" and method == "POST":
            if not self._authorized(headers, path): return self._json(cl, 401, {"ok": False})
            ts = headers.get("x-hearth-ts", "0")
            self._json(cl, 200, {"ok": True, "device_id": self.device_id,
                                 "token": self.auth.sign("/pair", ts)})
        else:
            self._json(cl, 404, {"ok": False})
        cl.close()

    def _clips_json(self):
        out = []
        try:
            for f in os.listdir(self.clips_dir):
                try: sz = os.stat(self.clips_dir + "/" + f)[6]
                except OSError: sz = 0
                out.append({"file": f, "ts": int(f.split(".")[0]) if f[0].isdigit() else 0, "size": sz})
        except OSError:
            pass
        return ujson.dumps(out)

    def _send_file(self, cl, fname):
        if ".." in fname: return self._json(cl, 400, {"ok": False})
        try:
            f = open(self.clips_dir + "/" + fname, "rb")
        except OSError:
            return self._json(cl, 404, {"ok": False})
        cl.send(b"HTTP/1.0 200 OK\r\nContent-Type: video/mp4\r\n\r\n")
        while True:
            chunk = f.read(1024)
            if not chunk: break
            cl.send(chunk)
        f.close()

    def _send(self, cl, code, ctype, body):
        cl.send(("HTTP/1.0 %d OK\r\nContent-Type: %s\r\n\r\n" % (code, ctype)).encode())
        cl.send(body.encode() if isinstance(body, str) else body)

    def _send_bytes(self, cl, code, ctype, data):
        cl.send(("HTTP/1.0 %d OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n"
                 % (code, ctype, len(data))).encode())
        cl.send(data)

    def _json(self, cl, code, obj):
        self._send(cl, code, "application/json", ujson.dumps(obj))
