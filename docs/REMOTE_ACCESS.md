# Remote Access & Mobile Gateway

How Polymath becomes reachable from a phone — on the home Wi‑Fi *and* from
anywhere on the internet — without port‑forwarding, dynamic DNS, or a VPN, while
staying true to the local‑first privacy stance in [PRIVACY.md](PRIVACY.md).

> **Status:** the client (`app/`), the relay (`cloud/relay/`), and the gateway
> module (`src/gateway/`) are built. Wiring the gateway into `Polymath.exe`
> (root CMake + `AppController`) is the deferred final step — see
> [WIRING.md](../WIRING.md).

---

## 1. Goals & constraints

| Goal | How it's met |
| --- | --- |
| iOS + Android + mobile web | One Capacitor/React codebase (`app/`) builds all three. |
| "Only our authorized users" | QR pairing → per‑device tokens; revocable from the desktop. No token = no access. |
| Easy setup | Scan a QR shown on the desktop. No router config, no certificates to manage. |
| Hosted on the home network | The gateway runs *inside* `Polymath.exe`; data never leaves the house unless you open the app from outside. |
| Reachable when away, no VPN | The home server makes an **outbound** WebSocket to a relay; phones reach it through the relay. Works behind NAT/CGNAT. |
| Local‑first / private | On Wi‑Fi the phone talks **directly** to the home server (no relay). The relay only ever forwards TLS frames it cannot read (optional E2E layer, §4). |

## 2. The three connection paths

```
                         ┌─────────────────────────── home network ───────────────────────────┐
                         │                                                                     │
  ┌────────────┐  LAN    │   ┌──────────────────────────── Polymath.exe ──────────────────┐    │
  │  Phone /   │ ───────────▶│  src/gateway:  QHttpServer + QWebSocketServer  (port 8765)  │    │
  │  Web app   │  (mDNS  │   │     ├─ REST  → AppController / SQLite                        │    │
  │  (app/)    │ polymath│   │     ├─ WS    → EventBus stream                               │    │
  │            │  .local)│   │     └─ RelayClient ──────────────┐ outbound WSS (dials out) │    │
  └─────┬──────┘         │   └──────────────────────────────────┼──────────────────────────┘    │
        │                └────────────────────────────────────  │  ──────────────────────────────┘
        │   relay (when away)                                    │
        │                       ┌───────────────────────────┐    │
        └──────────────────────▶│   cloud/relay  (WSS)      │◀───┘
                                │  routes frames by home_id │
                                │  never stores app data    │
                                └───────────────────────────┘
```

1. **LAN direct (fast path).** When the phone is on the same Wi‑Fi it resolves
   `polymath.local` (mDNS / Bonjour) and connects straight to the gateway on
   port `8765`. Lowest latency, zero third parties, fully local.
2. **Relay (remote path).** When away, the phone connects to the public relay,
   which forwards frames over the tunnel the home server already holds open.
   No inbound ports are opened on the home router.
3. **Self‑hosted relay (optional).** The relay is ~300 lines of Node; a
   privacy‑maximalist can run it on a $5 VPS or Fly.io/Render and point both the
   gateway and the app at it. See `cloud/relay/README.md`.

The client tries LAN first and transparently falls back to the relay
(`app/src/api/transport.ts`), so the same code works at home and away.

## 3. Pairing & authorization

```
Desktop (Settings ▸ Mobile Access)        Phone (app/)
─────────────────────────────────         ──────────────────────────────
 shows QR:                                  scan QR
   { relay_url, home_id,                     │
     pair_code (TTL 5 min) }                 ▼
                                    POST /api/v1/pair
                                      { code, device_name, pubkey }
        gateway verifies code  ◀──────────────┘
        creates a `devices` row
        issues device token (JWT, signed by gateway secret)
                                    ──────────────▶ { token, home_id, relay_url }
                                              store token in secure storage
 Every later request carries:  Authorization: Bearer <device token>
 Revoke any device from the desktop list → token rejected immediately.
```

* The pairing code is short‑lived and single‑use; the long‑lived **device
  token** is per‑device and individually revocable.
* The app can gate the stored token behind device biometrics (Face ID /
  fingerprint) so a lost phone doesn't grant access.
* Tokens are scoped (`role: owner|guest`) so a guest device can be limited to a
  subset of endpoints (e.g. chat but not cameras).

## 4. Privacy posture

* **At home, nothing leaves the house** — LAN path, no relay involved.
* **Away**, traffic is TLS end‑to‑end to the relay and tunnelled to the home
  server. The relay routes by opaque `home_id` and **never persists app data**.
* **Optional E2E**: a Noise/libsodium channel (`X25519 + ChaCha20‑Poly1305`)
  between app and gateway makes the relay a blind pipe that only sees
  ciphertext. The key is exchanged during pairing (the device `pubkey` above).
  Hook points are present in both `transport.ts` and `relay_client`; enabling it
  end‑to‑end is a finishing task (see WIRING.md §E2E).
* Remote access is **off by default**. The gateway only dials the relay once the
  user enables "Allow remote access" in the desktop UI.

## 5. Where each piece lives

| Component | Path | Language |
| --- | --- | --- |
| Mobile/web client | `app/` | React + TS + Capacitor |
| Reverse‑tunnel relay | `cloud/relay/` | Node + TS |
| Embedded gateway (HTTP+WS+relay client+auth) | `src/gateway/` | C++ / Qt |
| API contract (shared source of truth) | `app/src/api/contract.ts`, `docs/API.md` | — |

## 6. Setup (end‑user)

1. **Run the relay once** (or use the hosted default). `cd cloud/relay && npm i && npm start`, or deploy with the included `Dockerfile` / `fly.toml`.
2. In Polymath desktop, open **Settings ▸ Mobile Access**, toggle *Allow remote
   access*, and a QR appears.
3. Install the app (App Store / Play / or just open the web URL on the phone and
   "Add to Home Screen") and tap **Pair**, then scan the QR.
4. Done. The phone now works on Wi‑Fi and away. Manage/revoke devices from the
   same desktop screen.

See [MOBILE.md](MOBILE.md) for building the apps and [API.md](API.md) for the
endpoint reference.
