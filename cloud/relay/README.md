# Polymath relay

A thin, privacy-preserving **reverse-tunnel relay** that lets a Polymath home
server — sitting behind NAT/CGNAT with **no port-forwarding** — be reached from
phones over the internet.

The home gateway dials **out** to this relay and holds a single persistent
WebSocket open. Phones connect to the relay as if it were the home server; the
relay forwards their traffic over that one tunnel and returns the responses. No
inbound ports are ever opened on the home router (the ngrok / Cloudflare-Tunnel
pattern, in ~300 lines of Node).

It is part of the larger remote-access design in
[`docs/REMOTE_ACCESS.md`](../../docs/REMOTE_ACCESS.md). On the same Wi-Fi the app
talks to the home **directly** (mDNS) and never touches this relay at all — the
relay is only for the "away" path.

---

## The privacy boundary (read this)

**The relay is a blind pipe. It does not inspect, transform, or store
application data.**

- It forwards request/response **bodies** and the **`Authorization` header
  byte-for-byte, untouched**. It never parses, validates, or logs them.
- It **does not validate device tokens.** Authentication of end-user devices is
  the **gateway's** job, enforced end-to-end. A revoked token is rejected by the
  home, not the relay.
- The **only** thing the relay authenticates is **homes** — via a per-home
  secret in the tunnel handshake — so a random actor can't claim a `home_id` and
  hijack its traffic.
- It routes purely by opaque `home_id`. Nothing is persisted: no database, no
  disk writes, no request logging of payloads. Logs contain only routing
  metadata (`home_id`, method, path, status, message ids).
- With the optional end-to-end layer enabled (§4 of `REMOTE_ACCESS.md`), the
  bytes flowing through the relay are ciphertext it cannot read at all.

If you want maximum privacy, **run your own relay** (it's one container) so the
tunnel terminates on infrastructure you control.

---

## How it works

```
  Phone (away)                 Relay (public)                 Home gateway (NAT'd)
  ────────────                 ──────────────                 ────────────────────
   https://relay/h/:home_id/api/...  ─┐
                                      │  one persistent
                                      ▼  agent WebSocket
                              ┌───────────────┐  wss://relay/agent   ┌────────────┐
                              │  routes by    │◀─────────────────────│ RelayClient│
                              │   home_id     │   { t:'hello', … }   │  (dials    │
                              │  (this code)  │─────────────────────▶│   OUT)     │
                              └───────────────┘   framed JSON         └────────────┘
```

1. **Home connects out.** The gateway opens `wss://<relay>/agent` and sends a
   `hello` frame with its `home_id` + `secret`. The relay authenticates it and
   stores the live socket keyed by `home_id`. A reconnect **replaces** the
   previous socket for that home (old one is torn down).
2. **Phone reaches the home through the relay:**
   - **REST** — `https://<relay>/h/:home_id/api/...` → the relay buffers the
     request, ships it over the tunnel as a `req` frame, awaits the `res`, and
     writes it back.
   - **WebSocket** — any WS upgrade under `/h/:home_id/` (e.g.
     `wss://<relay>/h/:home_id/api/v1/events`) → the relay opens a logical
     channel over the tunnel and bridges frames both ways.
3. Everything is multiplexed over the single agent socket using framed JSON
   (see the protocol table below). The relay keeps a pending-request map
   (`id → resolver`, with a 30 s timeout) and a channel map (`cid → client WS`),
   and cleans both up if the tunnel drops.

### Routing summary

| Incoming                                   | Handled as                                   |
| ------------------------------------------ | -------------------------------------------- |
| `GET /healthz`                             | liveness probe → `200 {ok,tunnels}`          |
| `* /h/:home_id/<path>` (HTTP)              | REST, forwarded over the tunnel              |
| WS upgrade `→ /agent`                      | a home gateway dialing in (the tunnel)       |
| WS upgrade `→ /h/:home_id/<path>`          | a client WS, bridged to the home             |
| anything else                              | `404`                                        |

Client-facing error statuses: `503 home_offline` (no tunnel for that home),
`504 gateway_timeout` (home didn't answer in time), `502 bad_gateway` (tunnel
dropped mid-request), `413 payload_too_large`, `429 rate_limited`.

---

## Tunnel protocol (source of truth for the gateway author)

All messages are **JSON text frames** over the single agent WebSocket, tagged by
a discriminant field `t`. Bytes-carrying fields (`body`) are **base64** (binary
safe; `null` when empty). `path` is everything **after** `/h/:home_id` — so the
gateway sees the same path it would on the LAN (e.g. `/api/v1/status`). This
table is mirrored in [`src/protocol.ts`](src/protocol.ts) (the authoritative
TypeScript definitions).

### relay → gateway

| `t`          | Fields                                              | Meaning                                                              |
| ------------ | --------------------------------------------------- | ------------------------------------------------------------------- |
| `hello_ok`   | `now` (unix ms)                                     | Handshake accepted. Sent once after a valid `hello`.                |
| `req`        | `id`, `method`, `path`, `headers`, `body`           | Forwarded REST request. `path` **includes** the query string. `body` is base64 or `null`. `headers` is a flat map; **`Authorization` is forwarded untouched**. |
| `ws_open`    | `cid`, `path`, `query`, `headers`                   | Open an upstream WS for a client. `path` is **without** query; `query` is the raw query string **without** the leading `?` (may be `""`). |
| `ws_msg`     | `cid`, `data`                                       | One text frame for channel `cid` (verbatim payload).                |
| `ws_close`   | `cid`, `code?`, `reason?`                            | Close channel `cid`.                                                |
| `ping`       | —                                                   | App-level keepalive. Gateway should reply `pong`.                   |
| `pong`       | —                                                   | Reply to a gateway-initiated `ping`.                                |

### gateway → relay

| `t`           | Fields                                   | Meaning                                                                 |
| ------------- | ---------------------------------------- | ----------------------------------------------------------------------- |
| `hello`       | `home_id`, `secret`, `agent?`            | **First frame** after connecting to `/agent`. Authenticates the home.   |
| `res`         | `id`, `status`, `headers`, `body`        | Response for a prior `req`. `body` base64 or `null`. Binary safe (JPEG, etc.). |
| `ws_open_ok`  | `cid`                                    | Upstream WS for `cid` is open and ready.                                |
| `ws_err`      | `cid`, `error`                           | Upstream WS could not be opened; relay closes the client WS.            |
| `ws_msg`      | `cid`, `data`                            | One text frame from upstream for channel `cid`.                         |
| `ws_close`    | `cid`, `code?`, `reason?`                | Upstream closed channel `cid`.                                          |
| `ping`        | —                                        | App-level keepalive (relay replies `pong`).                             |
| `pong`        | —                                        | Reply to a relay `ping` (keeps the tunnel alive).                       |

### Gateway implementation notes

- **ids/cids are relay-assigned.** The gateway just echoes them back on the
  matching `res` / `ws_*` frames.
- **Reconnect freely.** If the socket drops, dial `/agent` again and re-`hello`;
  the relay replaces the old tunnel. The relay fails all in-flight `req`s for a
  dropped tunnel (clients get `502`).
- **Keepalive.** The relay sends an app-level `ping` **and** a WS-level ping
  every ~20 s; if neither a `pong` nor a WS pong arrives within ~10 s it drops
  the tunnel. Answer `ping` with `pong` (or just let the WS-level pong handle
  it).
- **Handshake close codes** the relay uses on rejection: `4001` hello timeout,
  `4002` first frame wasn't a `hello`, `4401` auth failed.
- **`path` for `req` includes the query**; for `ws_open` the `query` is carried
  separately (so the gateway can rebuild its upstream URL exactly).

---

## Caveats

- **No live MJPEG streaming over `req`/`res`.** Camera *snapshots* work fine (a
  normal request/response, even large binary bodies). A continuous
  `multipart/x-mixed-replace` MJPEG stream does **not** — the relay buffers a
  full response before returning it, so an infinite stream would never complete.
  The app handles this by **polling snapshots** off-LAN instead. Long-poll and
  the **events WebSocket work** via the WS bridge.
- **Text WS frames.** The bridge forwards WebSocket **text** frames (the
  Polymath events protocol is JSON). Binary client frames are coerced to UTF-8
  best-effort.
- **Open mode is in-memory only.** `RELAY_OPEN=true` pins the first secret it
  sees per `home_id` until the process restarts (then it re-pins). Fine for
  self-hosting; use `RELAY_HOMES` for anything shared.
- **Run at least one always-on instance.** The tunnel is a long-lived socket, so
  scale-to-zero hosting will sever it. The Fly/Render configs here keep one
  instance running.

---

## Run it locally

Requires Node ≥ 18.

```bash
cd cloud/relay
npm install

# Dev (auto-reload). Open mode = trust-on-first-use, convenient for testing.
RELAY_OPEN=true npm run dev

# …or build + start.
npm run build
RELAY_OPEN=true npm start
```

Sanity check:

```bash
curl localhost:8080/healthz        # → {"ok":true,"tunnels":0}
```

Point the desktop gateway's relay URL at `ws://localhost:8080` (or
`wss://…` once deployed) and toggle **Allow remote access**; you should see
`agent tunnel established` in the relay log.

### Configuration

Everything is environment-driven — see [`.env.example`](.env.example) for the
full annotated list. The essentials:

| Var                  | Default     | Purpose                                                        |
| -------------------- | ----------- | -------------------------------------------------------------- |
| `PORT`               | `8080`      | Public HTTP/WS listen port.                                    |
| `RELAY_OPEN`         | `false`     | Trust-on-first-use home auth (in-memory). For self-hosting.    |
| `RELAY_HOMES`        | `{}`        | JSON allow-list `{ "<home_id>": "<secret>" }`.                 |
| `RELAY_HOMES_FILE`   | —           | Path to a JSON file of the same shape (merged; env wins).      |
| `REQUEST_TIMEOUT_MS` | `30000`     | Wait for the home to answer a request before `504`.            |
| `RATE_LIMIT_PER_SEC` | `50`        | Per-home token-bucket rate (set `0` to disable).               |
| `RATE_LIMIT_BURST`   | `100`       | Per-home burst capacity.                                       |
| `MAX_BODY_BYTES`     | `10485760`  | Max buffered client request body before `413`.                |
| `LOG_LEVEL`          | `info`      | `debug` \| `info` \| `warn` \| `error`.                       |

---

## Register a home

A "home" is identified by an opaque `home_id` (shown in the desktop's *Mobile
Access* QR) and authenticated by a shared `secret`. Two ways to allow one:

**1. Static allow-list (recommended for a shared relay).** Generate a strong
secret and add it to `RELAY_HOMES`:

```bash
SECRET=$(openssl rand -hex 32)
echo "$SECRET"     # paste this into the gateway's relay settings on the desktop

export RELAY_HOMES='{"home-abc123":"'"$SECRET"'"}'
npm start
```

or keep them in a file and point `RELAY_HOMES_FILE` at it:

```json
// homes.json  (git-ignored)
{
  "home-abc123": "…64 hex chars…",
  "home-def456": "…another…"
}
```

The same `home_id` + `secret` go into the desktop gateway so its outbound
`hello` matches.

**2. Trust-on-first-use (self-host convenience).** Set `RELAY_OPEN=true`. The
first secret presented for each `home_id` is accepted and pinned in memory.
Simplest for a relay only your own home uses; **don't** use it on a relay others
can reach. (The relay logs a warning when open mode is on.)

---

## Deploy

The included [`Dockerfile`](Dockerfile) is a multi-stage Node 20 build → slim,
non-root runtime.

### Fly.io

```bash
cd cloud/relay
fly launch --no-deploy            # create the app (edit fly.toml app name/region)
fly secrets set RELAY_HOMES='{"home-abc123":"<long-random-secret>"}'
fly deploy
```

Fly terminates TLS and proxies to the internal port; your relay URL is
`wss://<app>.fly.dev`. Health check hits `/healthz`. The config keeps one
machine always running so the tunnel isn't dropped.

### Render

Commit the repo, then **New → Blueprint** and point Render at
[`render.yaml`](render.yaml). Add `RELAY_HOMES` as a secret env var in the
dashboard. Your relay URL is `wss://<service>.onrender.com`.

### Any Docker host / VPS

```bash
docker build -t polymath-relay cloud/relay
docker run -d --restart unless-stopped -p 8080:8080 \
  -e RELAY_HOMES='{"home-abc123":"<long-random-secret>"}' \
  --name polymath-relay polymath-relay
```

Put it behind a TLS-terminating reverse proxy (Caddy, nginx, Cloudflare) so the
public URL is `wss://…`, and point both the gateway and the app at it.
