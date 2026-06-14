# hearth_id — device_id = "hearth-cam-<hex6>" from the K230 MAC (FABRIC.md §1).
import network

_cached = None

def mac_hex6():
    try:
        wlan = network.WLAN(network.STA_IF)
        wlan.active(True)
        mac = wlan.config('mac')            # 6 raw bytes
    except Exception:
        mac = b'\x00\x00\x00\x00\x00\x00'
    return "{:02x}{:02x}{:02x}".format(mac[3], mac[4], mac[5])

def device_id():
    global _cached
    if _cached is None:
        _cached = "hearth-cam-" + mac_hex6()
    return _cached
