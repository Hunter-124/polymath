# WIRING — integrating the mobile gateway into Hearth.exe

The mobile stack ships as three self-contained pieces that are **done and
verified independently**:

| Piece | Path | State |
| --- | --- | --- |
| Mobile/web client | `app/` | ✅ typechecks + `vite build` produces a PWA |
| Reverse-tunnel relay | `cloud/relay/` | ✅ typechecks + smoke-tested end-to-end |
| Embedded gateway (Qt) | `src/gateway/` | ✅ written; compiles once wired (no Qt toolchain in this env) |

What remains is **gluing the gateway into the app build + `AppController`**. These
are the deliberately-deferred "finishing details." Do them in order; each box is
a discrete step. Copy-pasteable snippets live in
[`src/gateway/README.md`](src/gateway/README.md).

---

## A. Build wiring

- [ ] **vcpkg** — add the two Qt modules the gateway needs to `vcpkg.json`'s
  `qtbase`/features (or as deps): `qthttpserver`, `qtwebsockets`.
- [ ] **Root `CMakeLists.txt`** —
  ```cmake
  find_package(Qt6 REQUIRED COMPONENTS Core Network HttpServer WebSockets)
  add_subdirectory(src/gateway)
  # link into the app/executable target that owns AppController:
  target_link_libraries(pm_app PRIVATE pm_gateway)
  ```
- [ ] Confirm `nlohmann_json` is visible to `pm_gateway` (it is already a project
  dep; the module's `CMakeLists.txt` links it).

## B. AppController wiring  (`src/app/app_controller.{h,cpp}`)

- [ ] **Implement `IAssistantBridge`** (`src/gateway/bridge.h`). All 11 methods map
  1:1 onto existing `AppController` members. The only change: have **`sendChat`
  return the `request_id`** (factor the rid generation out of `sendText`).
- [ ] **Promote `Config` to a member.** It's currently a local in
  `initialize()`; the gateway holds a `Config&` for its lifetime, so it must
  outlive the call.
- [ ] **Construct + thread the service:**
  ```cpp
  gateway_ = std::make_unique<GatewayService>(*this /*IAssistantBridge*/, db_, config_);
  runOnThread(gateway_.get(), gateway_.get());   // same helper the other services use
  ```
  Add `gateway_`'s thread to `threads_` so `shutdown()` joins it.
- [ ] **Tear down** in `shutdown()` before the DB closes.

## C. Desktop UI — pairing screen

- [ ] Add a **`Settings ▸ Mobile Access`** QML view that:
  - binds a *Allow remote access* toggle to `GatewayService::setRemoteEnabled(bool)`;
  - shows the pairing QR by rendering `GatewayService::pairingDeepLink()`
    (`polymath://pair?…`). Qt has no built-in QR painter — add a tiny encoder
    (e.g. `nayuki-qr` single-file, or `qrcodegen` via vcpkg) and draw to a
    `QImage`/`QQuickImageProvider`.
  - lists paired devices via the `/devices` data and offers revoke.
- [ ] Expose `GatewayService` (or just these methods) to QML as a context
  property, mirroring how `AppController` is exposed as `app`.

## D. Schema tidy

- [ ] The gateway self-creates its `devices` table via `gateway_db.cpp`
  (`CREATE TABLE IF NOT EXISTS …`). Optionally **fold that table into
  `src/core/schema.h`** and bump `kSchemaVersion` so it lives with the rest of
  the canonical schema. (Leaving it as-is also works.)

## E. Remote access (relay) — deploy

- [ ] Deploy `cloud/relay/` once: `Dockerfile`, `fly.toml`, or `render.yaml` are
  included. Note its public `wss://…` URL.
- [ ] Set `gateway.relay_url` (generated into the `settings` table on first run)
  to that URL — either via the Mobile Access UI or a one-time settings write.
  Self-hosters can also run it on the LAN.

## F. Finishing details (nice-to-have, not blocking)

- [ ] **mDNS** — advertise `polymath.local` / `_polymath._tcp` so the phone
  finds the LAN fast-path by name instead of IP. (Windows: Bonjour SDK or a
  small DNS-SD lib. Until then, LAN pairing can embed the host's IP in the QR —
  the QR payload already carries `lan_host`.)
- [ ] **PWA icons** — drop `icon-192.png`, `icon-512.png`,
  `icon-512-maskable.png` into `app/public/icons/` (see the README there).
- [ ] **Native app packaging** — `npm run add:ios` / `add:android`, set bundle
  IDs/signing, then `cap sync`. For iOS QR scanning add
  `NSCameraUsageDescription` to `Info.plist`.
- [ ] **Push notifications** — wire reminders to APNs/FCM via
  `@capacitor/push-notifications` so reminders fire when the app is closed.
- [ ] **End-to-end encryption (optional, privacy max).** The pairing handshake
  already exchanges an X25519 `pubkey`. Implement a Noise/libsodium channel
  (`X25519 + ChaCha20-Poly1305`) between `app/src/api/transport.ts` and the
  gateway so the relay only ever sees ciphertext. Hook points are marked in both.
- [ ] **Voice over mobile** — `/api/v1/voice` is reserved in the contract; add
  `MediaRecorder` capture in `app/` → POST → ASR → existing chat flow, and play
  `speak` events' TTS.

---

### Verify after wiring

1. Build `Hearth.exe` (CUDA preset). On launch, the gateway binds `:8765`.
2. `cd app && npm run dev`, open `http://localhost:5173` (the dev server proxies
   `/api` → `:8765`). Pair using the desktop QR, confirm chat streams, cameras
   load, lists sync.
3. Enable remote access, then load the app over cellular (or another network) —
   it should transparently fall back to the relay.
4. Revoke the device on the desktop → the app drops to the pairing screen on its
   next request (401 → auto-unpair).
