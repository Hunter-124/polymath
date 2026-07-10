# WIRING — mobile gateway integration status

The mobile stack ships as three pieces. Core desktop wiring is **done**; only
optional niceties remain.

| Piece | Path | State |
| --- | --- | --- |
| Mobile/web client | `app/` | ✅ typechecks + `vite build` produces a PWA |
| Reverse-tunnel relay | `cloud/relay/` | ✅ typechecks + smoke-tested end-to-end |
| Embedded gateway (Qt) | `src/gateway/` | ✅ built into Polymath, started from AppController |

---

## A. Build wiring — ✅ done

- [x] Qt HttpServer / WebSockets available (project Qt kit + gateway CMake).
- [x] `add_subdirectory(src/gateway)` + `pm_app` links `pm_gateway`.
- [x] `nlohmann_json` visible to `pm_gateway`.

## B. AppController wiring — ✅ done

- [x] `AppBridge` implements `IAssistantBridge` (`src/app/app_bridge.*`).
- [x] `Config` is a long-lived member of `AppController`.
- [x] `GatewayService` constructed, started, exposed as QML `gateway`.
- [x] Tear-down in `shutdown()` before DB close.

## C. Desktop UI — pairing screen — ✅ done

- [x] `MobileAccessView.qml` (remote toggle, pairing QR, device list).
- [x] `gateway` context property on the QML engine.

## D. Schema tidy — optional

- [x] Gateway creates `devices` via `gateway_db.cpp` (`CREATE TABLE IF NOT EXISTS`).
- [ ] Optionally fold `devices` into `src/core/schema.h` (not required for ship).

## E. Remote access (relay) — operator step

- [ ] Deploy `cloud/relay/` once; set `gateway.relay_url` in settings / Mobile Access UI.
  Self-hosters can use LAN only without a public relay.

## F. Nice-to-have (not blocking ship)

- [ ] **mDNS** — `polymath.local` / `_polymath._tcp`.
- [ ] **PWA icons** — `app/public/icons/icon-192.png` etc.
- [ ] **Native app packaging** — Capacitor iOS/Android.
- [ ] **Push notifications** — APNs/FCM for reminders when backgrounded.
- [ ] **E2E encryption** over the relay (Noise / libsodium).
- [ ] **Voice over mobile** — `/api/v1/voice` MediaRecorder path.

---

### Verify

1. Launch Polymath → log shows `gateway: started (port=8765, …)`.
2. Mobile Access page shows pairing QR / LAN host.
3. Optional: deploy relay, set `gateway.relay_url`, pair a phone.
