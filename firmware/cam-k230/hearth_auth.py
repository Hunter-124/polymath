# hearth_auth — per-device HMAC bearer + pairing QR payload (FABRIC.md §6).
# Bearer = base64url(HMAC-SHA256(key, path + "." + ts)); same scheme as the
# ESP32 firmware/common hearth_auth and the hub gateway.
import os, ubinascii, hashlib, hmac, ujson

_KEY_PATH = "/sdcard/hearth/devkey.bin"

class Auth:
    def __init__(self):
        self.key = self._load_or_make()

    def _load_or_make(self):
        try:
            with open(_KEY_PATH, "rb") as f:
                k = f.read()
                if len(k) == 32:
                    return k
        except OSError:
            pass
        k = os.urandom(32)
        try:
            try: os.mkdir("/sdcard/hearth")
            except OSError: pass
            with open(_KEY_PATH, "wb") as f:
                f.write(k)
        except OSError:
            pass
        return k

    def key_b64(self):
        return ubinascii.b2a_base64(self.key).decode().strip()

    def _b64url(self, raw):
        s = ubinascii.b2a_base64(raw).decode().strip()
        return s.replace('+', '-').replace('/', '_').rstrip('=')

    def sign(self, path, ts):
        msg = (path + "." + str(ts)).encode()
        return self._b64url(hmac.new(self.key, msg, hashlib.sha256).digest())

    def verify(self, auth_header, path, ts, now_unix, skew=120):
        if not auth_header:
            return False
        tok = auth_header.strip()
        if tok.lower().startswith("bearer "):
            tok = tok[7:].strip()
        if now_unix and abs(now_unix - int(ts)) > skew:
            return False
        expected = self.sign(path, ts)
        if len(expected) != len(tok):
            return False
        diff = 0
        for a, b in zip(expected, tok):
            diff |= ord(a) ^ ord(b)
        return diff == 0

    def qr_payload(self, device_id, kind, softap, lan_host):
        # FABRIC.md §6 exact fields.
        return ujson.dumps({
            "v": 1, "device_id": device_id, "kind": kind,
            "key": self.key_b64(), "softap": softap, "lan_host": lan_host,
        })
