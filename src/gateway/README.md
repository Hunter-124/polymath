# pm_gateway — mobile/web gateway (HTTP + WebSocket + relay tunnel + auth)

Embeds a small server **inside `Polymath.exe`** that exposes the existing
assistant to the mobile/web client (`app/`):

- **REST** over `QHttpServer` on `0.0.0.0:8765` — every endpoint in
  `app/src/api/contract.ts` (`ENDPOINTS`).
- **WebSocket** event stream at `GET /api/v1/events` — the `EventBus` signal
  catalog re-broadcast as `ServerEvent` envelopes, with per-client topic/camera
  subscriptions.
- **Outbound relay tunnel** — when *Allow remote access* is on, dials
  `cloud/relay` over an outbound WSS so phones reach the home from anywhere with
  **no inbound ports** (works behind NAT/CGNAT). Mirrors the framed-JSON protocol
  in `cloud/relay/src/protocol.ts`.
- **Device-token auth** — QR pairing → per-device HMAC tokens, revocable from the
  desktop. No token ⇒ no access. See `docs/REMOTE_ACCESS.md`.

The module depends on **`pm_core` + Qt only** — never `pm_app` — to avoid a
dependency cycle. It reaches the rest of the app through one abstract seam,
`IAssistantBridge` (`bridge.h`), which `AppController` implements during wiring.

> **Qt 6.5+** (the repo's `qt_standard_project_setup(REQUIRES 6.5)` floor; the
> code also guards a couple of 6.7/6.8 API shifts). Like the rest of the repo,
> this **won't compile in this environment** — there's no Qt/CUDA toolchain
> here. It builds as part of the normal CMake/vcpkg toolchain once wired in.

---

## Files

| File | Purpose |
| --- | --- |
| `bridge.h` | `IAssistantBridge` — the abstract actions/status the API needs. **AppController implements this.** |
| `gateway_db.{h,cpp}` | `devices` table + `gateway.*` settings (home_id, secret, relay_url, remote_enabled, port). Generates home_id/secret on first run. |
| `auth.{h,cpp}` | Device tokens (`base64url(payload).base64url(HMAC-SHA256)` via `QMessageAuthenticationCode`), single-use pairing codes (in-memory, 5-min TTL), device CRUD. |
| `json_map.{h,cpp}` | Row/core-type ⇄ `nlohmann::json` DTOs, exactly matching `contract.ts`. |
| `http_router.{h,cpp}` | Transport-agnostic `Response handle(method, path, headers, body)` implementing every endpoint. **Both the local server and the relay call this.** |
| `http_server.{h,cpp}` | `QHttpServer` + `QWebSocketServer` on `0.0.0.0:<port>`; upgrades `/api/v1/events` to a `WsHub` client. |
| `ws_hub.{h,cpp}` | Connected clients (real sockets + relay-bridged `IClientChannel`s); subscribes to `EventBus`; fan-out + control protocol. |
| `relay_client.{h,cpp}` | Outbound `QWebSocket` to `<relay>/agent`; `hello` handshake, backoff reconnect, home side of the tunnel. |
| `gateway_service.{h,cpp}` | `GatewayService : QObject, IService` — assembles everything; exposes pairing payload + deep link for the desktop QR. |
| `CMakeLists.txt` | `pm_gateway` static lib. |

---

## Public surface `AppController` wires to

### 1. Implement `IAssistantBridge` (`bridge.h`)

Every method maps 1:1 onto an existing `AppController` member. The one wrinkle is
`sendChat`, which must **return the request_id**, so factor the rid out of
`AppController::sendText`:

```cpp
// app_controller.h  — add a base class (or a tiny adapter that forwards to it):
class AppController : public QObject, public IAssistantBridge {
    // ...
public:
    QString sendChat(const QString& text) override;           // returns request_id
    void    setPersonality(const QString& n) override { setPersonalityImpl(n); }
    QStringList personalities() override { /* existing body */ }
    void    setPrivacy(const QString& k, bool on) override { /* existing body */ }
    bool    privacy(const QString& k) override { /* existing body */ }
    void    findObject(const QString& q) override { /* existing body */ }
    void    addShoppingItem(const QString& i) override { /* existing body */ }
    QVariantList models() override { /* existing body */ }
    bool    listening() override { return listening_; }
    QString activePersonality() override { return active_personality_; }
    QString modelStatus() override { return model_status_; }
};
```

`sendChat` should mint the rid and dispatch to the agent worker, returning it:

```cpp
QString AppController::sendChat(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return {};
    if (chat_model_) chat_model_->appendUser(t);
    const QString rid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QMetaObject::invokeMethod(agent_.get(), "handleTextInput", Qt::QueuedConnection,
                              Q_ARG(QString, t), Q_ARG(QString, rid));
    return rid;   // the gateway returns this to the client as ChatSendResponse.request_id
}
```

(The existing void `sendChat`/`sendText` can delegate to this and ignore the
return.) The bridge methods are called from the gateway worker thread, so keep
the getters lock-free (they already touch only members/atomics + WAL-mode DB);
the action methods already marshal onto workers via `QMetaObject::invokeMethod`.

### 2. Construct `GatewayService` on its own `QThread`

```cpp
// in AppController::initialize(), after services are built:
#include "gateway_service.h"
gateway_ = std::make_unique<GatewayService>(*this /*IAssistantBridge*/, db_, cfg);
threads_.push_back(runOnThread(gateway_.get(), gateway_.get()));   // service.h helper
```

`cfg` is the `Config` already constructed in `initialize()` — keep it alive as a
member (today it's a local), or construct a fresh `Config(db_)` for the gateway.
`stop()`/`shutdown()` is handled by the existing `threads_` quit+wait loop.

### 3. Add a `Settings ▸ Mobile Access` QML view

Expose the service (or just its two helpers) to QML and render a QR from the
deep link / payload:

```cpp
engine.rootContext()->setContextProperty("gateway", gateway_.get());
```

```qml
// MobileAccessView.qml
Switch {
    text: "Allow remote access"
    checked: gateway.remoteEnabled()
    onToggled: gateway.setRemoteEnabled(checked)
}
// Render gateway.pairingDeepLink() (or gateway.pairingPayloadJson()) as a QR.
// Each call mints a fresh single-use code (TTL 5 min) — re-call on view show.
Text { text: "Connected devices: " + gateway.connectedClients() }
```

A QR generator isn't bundled (no extra dep added). Options: a tiny QML
`qrcode`-style component, a `qrencode`/`QZXing` dependency, or paint it in C++.

---

## DEFERRED WIRING — exact steps

1. **`vcpkg.json`** — add the Qt HTTP-server + WebSockets features so the Qt6
   `HttpServer`/`WebSockets` `CONFIG` packages are present:
   ```json
   "qthttpserver",
   "qtwebsockets"
   ```
   (Add both to the top-level `dependencies` array.)

2. **Root `CMakeLists.txt`** — request the two Qt components and add the
   subdirectory **after `src/app`** (so `pm_app` is defined before we link them):
   ```cmake
   # line ~25: append HttpServer WebSockets to the existing find_package
   find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2
                Multimedia Concurrent Network HttpServer WebSockets)
   # after add_subdirectory(src/app):
   add_subdirectory(src/gateway)
   ```
   (`src/gateway/CMakeLists.txt` also `find_package`s `HttpServer WebSockets`
   locally, so adding them at the root is belt-and-suspenders but keeps all Qt
   components discovered in one place.)

3. **`src/app/CMakeLists.txt`** — link the gateway into the app library:
   ```cmake
   target_link_libraries(pm_app PUBLIC
       pm_core pm_inference pm_scheduler pm_audio pm_vision
       pm_memory pm_agent pm_personality pm_ui pm_gateway   # <-- add pm_gateway
       Qt6::Qml Qt6::Quick)
   ```

4. **`AppController`** — items 1–3 above (implement `IAssistantBridge`, own a
   `std::unique_ptr<GatewayService> gateway_`, construct it on a thread, register
   it with the QML engine).

5. **`src/core/schema.h`** — fold the `devices` table into `kSchemaSQL` and bump
   `kSchemaVersion`, so it migrates through the normal path. Until then
   `GatewayDb::ensureSchema()` creates it lazily at startup (idempotent), so the
   module works either way; this is just cleanup.

6. **mDNS / Bonjour** — advertise `_polymath._tcp` as `polymath.local` on port
   `8765` so the app's LAN fast-path resolves it (`docs/REMOTE_ACCESS.md` §2,
   `app/src/api/transport.ts`). A finishing detail; the relay path works without
   it, and the pairing payload already carries the resolved `lan_host`/`lan_port`
   as a fallback.

---

## Notes, deviations & decisions

- **Relay protocol — followed the code, not the prompt.** The brief sketched
  `ws_open` as `{cid,path,headers}`, but the authoritative
  `cloud/relay/src/protocol.ts` carries a **separate `query` field** on
  `ws_open` (so `?token=` is split from the path), and adds `hello_ok` and
  `ws_err` frames plus optional `code`/`reason` on `ws_close`. `relay_client.cpp`
  mirrors **`protocol.ts`** exactly: it reads `query` for the WS token, waits for
  `hello_ok` before declaring the tunnel up, replies `ws_err` when it can't open
  a channel, and answers/sends app-level `ping`/`pong`. `cloud/relay/README.md`
  did not exist at authoring time; `protocol.ts`/`tunnel.ts` were used as the
  source of truth.

- **Auth gate.** Every route requires `Authorization: Bearer <token>` **except**
  `POST /api/v1/pair` and `GET /api/v1/status`. GET media routes
  (`/cameras/:id/snapshot|stream`, `/timeline/:id/thumb`) also accept
  `?token=…`, because `<img>`/`<video>` tags can't set headers. The WS handshake
  reads the token from `?token=` and verifies it before the client is accepted.

- **Token = HMAC, but revocable.** Tokens are stateless to verify (HMAC-SHA256
  over the payload with `gateway.secret`) yet revocable: `verifyToken` also
  requires the `device_id` to still have a `devices` row, so deleting the row
  (`DELETE /devices/:id`) rejects the token immediately.

- **Camera media.** Snapshots/thumbnails are served as JPEG bytes from
  `events.thumb_path` (resolved against `Paths::media()`), keeping the raw camera
  URL off the wire and working over the relay. A true multipart-MJPEG `/stream`
  proxy is left as a finishing detail — `/stream` currently returns the latest
  frame (so an `<img>` renders), and live video is better delivered via the WS
  `frame` events (subscribe a camera). The schema exposes no per-camera live
  buffer to proxy directly.

- **Chat history** reads the `transcripts` table (the only stored turn log).
  There's no explicit role column, so speaker-`NULL` rows map to `assistant` and
  the rest to `user` — a best-effort mapping noted in code.

- **DTO field names** match `contract.ts` byte-for-byte (snake_case). Extra WS
  event types beyond the brief's list (`speak`, `utterance`, `frame`) are all
  implemented since `ServerEventType` in the contract includes them.

- **Privacy/remote posture.** Remote access is **off by default**
  (`gateway.remote_enabled = 0`); the tunnel only dials once the user enables it.
  `setPrivacy` writes route through `IAssistantBridge` so they also emit
  `PrivacyChanged` (clients receive a `privacy` event), staying consistent with
  the desktop toggles. The optional E2E (X25519/ChaCha20) layer is advertised as
  `capabilities.e2e = false`; the device `pubkey` is captured at pairing for when
  it's enabled (`REMOTE_ACCESS.md` §4).
