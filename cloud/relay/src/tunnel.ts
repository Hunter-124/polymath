//
// tunnel.ts — the agent-connection manager.
//
// One AgentTunnel wraps the single persistent WebSocket a home gateway dials out
// to (wss://<relay>/agent). It owns:
//   - the hello/auth handshake,
//   - the pending-request map (id → waiter) with per-request timeouts,
//   - the channel map (cid → Ws bridge callbacks) for WebSocket forwarding,
//   - send/await-response helpers used by proxy.ts,
//   - app-level ping/pong + ws-level ping liveness, dropping dead tunnels,
//   - cleanup of all of the above on close (no leaks).
//
// TunnelManager holds Map<home_id, AgentTunnel> and enforces one live tunnel per
// home (a reconnect replaces and tears down the old one).
//

import { randomUUID } from 'node:crypto';

import type { WebSocket } from 'ws';

import type { Config } from './config.js';
import { log } from './log.js';
import type { Registry } from './registry.js';
import {
  parseFrame,
  type RawMsg,
  type RelayToAgent,
  type ResMsg,
} from './protocol.js';

/** A fully-buffered HTTP response handed back to proxy.ts. */
export interface TunnelResponse {
  status: number;
  headers: Record<string, string>;
  /** Decoded body bytes (may be empty). Binary-safe. */
  body: Buffer;
}

/** Callbacks proxy.ts registers to receive events for one bridged WS channel. */
export interface ChannelHandlers {
  /** Upstream confirmed the channel is open. */
  onOpenOk(): void;
  /** Upstream refused to open the channel. */
  onError(error: string): void;
  /** A text frame arrived from upstream; deliver to the client. */
  onMessage(data: string): void;
  /** Upstream closed the channel; close the client side. */
  onClose(code?: number, reason?: string): void;
}

interface PendingRequest {
  resolve(res: TunnelResponse): void;
  reject(err: Error): void;
  timer: NodeJS.Timeout;
}

/**
 * AgentTunnel — the relay's view of one connected home gateway.
 */
export class AgentTunnel {
  readonly homeId: string;
  private readonly ws: WebSocket;
  private readonly cfg: Config;

  /** id → in-flight REST waiter. */
  private readonly pending = new Map<string, PendingRequest>();
  /** cid → bridged client WS handlers. */
  private readonly channels = new Map<string, ChannelHandlers>();

  private pingTimer: NodeJS.Timeout | null = null;
  private pongDeadline: NodeJS.Timeout | null = null;
  private closed = false;

  /** Set by TunnelManager so we can de-register ourselves on close. */
  onClosed: (() => void) | null = null;

  constructor(ws: WebSocket, homeId: string, cfg: Config) {
    this.ws = ws;
    this.homeId = homeId;
    this.cfg = cfg;

    ws.on('message', (data) => this.onFrame(data));
    ws.on('close', (code, reason) => this.handleSocketClosed(code, reason.toString()));
    ws.on('error', (err) => {
      log.warn('agent ws error', { home_id: this.homeId, error: String(err) });
      // 'close' fires after 'error'; cleanup happens there.
    });
    // ws-level pong resets the liveness deadline too.
    ws.on('pong', () => this.armPongDeadline());

    this.startKeepalive();
  }

  // ─────────────────────────── REST ────────────────────────────────────────

  /**
   * Forward a buffered HTTP request and await the agent's response.
   * Rejects on timeout (caller maps to 504) or if the tunnel drops mid-flight
   * (caller maps to 502).
   */
  request(
    method: string,
    path: string,
    headers: Record<string, string>,
    body: Buffer | null,
  ): Promise<TunnelResponse> {
    const id = randomUUID();
    return new Promise<TunnelResponse>((resolve, reject) => {
      if (this.closed) {
        reject(new Error('tunnel_closed'));
        return;
      }
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error('timeout'));
      }, this.cfg.requestTimeoutMs);
      // Don't keep the event loop alive solely for this timer.
      timer.unref?.();

      this.pending.set(id, { resolve, reject, timer });

      this.send({
        t: 'req',
        id,
        method,
        path,
        headers,
        body: body && body.length > 0 ? body.toString('base64') : null,
      });
    });
  }

  // ─────────────────────── WebSocket bridging ──────────────────────────────

  /**
   * Ask the agent to open an upstream WS channel. Returns the relay-assigned
   * cid; the supplied handlers receive open_ok / err / msg / close events.
   * Returns null if the tunnel is already closed.
   */
  openChannel(
    path: string,
    query: string,
    headers: Record<string, string>,
    handlers: ChannelHandlers,
  ): string | null {
    if (this.closed) return null;
    const cid = randomUUID();
    this.channels.set(cid, handlers);
    this.send({ t: 'ws_open', cid, path, query, headers });
    return cid;
  }

  /** Forward a client text frame to upstream over an open channel. */
  sendChannelMessage(cid: string, data: string): void {
    if (!this.channels.has(cid)) return;
    this.send({ t: 'ws_msg', cid, data });
  }

  /**
   * Close a channel from the client side. Tells the agent and forgets the
   * channel locally. Does NOT invoke handlers.onClose (the client is already
   * closing).
   */
  closeChannel(cid: string, code?: number, reason?: string): void {
    if (!this.channels.delete(cid)) return;
    this.send({
      t: 'ws_close',
      cid,
      ...(code !== undefined ? { code } : {}),
      ...(reason !== undefined ? { reason } : {}),
    });
  }

  // ─────────────────────────── Frame handling ──────────────────────────────

  private onFrame(data: unknown): void {
    // We only speak text JSON on the tunnel. Coerce Buffer→string.
    const raw =
      typeof data === 'string'
        ? data
        : Buffer.isBuffer(data)
          ? data.toString('utf8')
          : Array.isArray(data)
            ? Buffer.concat(data as Buffer[]).toString('utf8')
            : String(data);

    const msg = parseFrame(raw);
    if (!msg) {
      log.warn('agent sent unparseable frame', { home_id: this.homeId });
      return;
    }

    switch (msg.t) {
      case 'res':
        this.handleRes(msg);
        break;
      case 'ws_open_ok':
        this.dispatchChannel(msg, (h) => h.onOpenOk());
        break;
      case 'ws_err':
        this.handleWsErr(msg);
        break;
      case 'ws_msg':
        this.handleWsMsg(msg);
        break;
      case 'ws_close':
        this.handleWsClose(msg);
        break;
      case 'ping':
        // Agent-initiated app-level ping; answer immediately.
        this.send({ t: 'pong' });
        break;
      case 'pong':
        // App-level pong — agent is alive.
        this.armPongDeadline();
        break;
      case 'hello':
        // Unexpected second hello on an established tunnel; ignore.
        log.warn('agent sent duplicate hello', { home_id: this.homeId });
        break;
      default:
        log.warn('agent sent unknown frame type', { home_id: this.homeId, t: String(msg.t) });
    }
  }

  private handleRes(msg: RawMsg): void {
    const id = typeof msg.id === 'string' ? msg.id : undefined;
    if (!id) return;
    const waiter = this.pending.get(id);
    if (!waiter) {
      // Late/duplicate response (already timed out) — drop it.
      return;
    }
    this.pending.delete(id);
    clearTimeout(waiter.timer);

    const res = msg as unknown as ResMsg;
    const status = typeof res.status === 'number' ? res.status : 502;
    const headers =
      res.headers && typeof res.headers === 'object' ? res.headers : {};
    const body =
      typeof res.body === 'string' ? Buffer.from(res.body, 'base64') : Buffer.alloc(0);

    waiter.resolve({ status, headers, body });
  }

  private handleWsErr(msg: RawMsg): void {
    const cid = typeof msg.cid === 'string' ? msg.cid : undefined;
    if (!cid) return;
    const h = this.channels.get(cid);
    if (!h) return;
    this.channels.delete(cid);
    h.onError(typeof msg.error === 'string' ? msg.error : 'upstream_error');
  }

  private handleWsMsg(msg: RawMsg): void {
    const cid = typeof msg.cid === 'string' ? msg.cid : undefined;
    const data = typeof msg.data === 'string' ? msg.data : undefined;
    if (!cid || data === undefined) return;
    const h = this.channels.get(cid);
    if (!h) return;
    h.onMessage(data);
  }

  private handleWsClose(msg: RawMsg): void {
    const cid = typeof msg.cid === 'string' ? msg.cid : undefined;
    if (!cid) return;
    const h = this.channels.get(cid);
    if (!h) return;
    this.channels.delete(cid);
    const code = typeof msg.code === 'number' ? msg.code : undefined;
    const reason = typeof msg.reason === 'string' ? msg.reason : undefined;
    h.onClose(code, reason);
  }

  /** Look up a channel by cid from a raw msg and run fn on its handlers. */
  private dispatchChannel(msg: RawMsg, fn: (h: ChannelHandlers) => void): void {
    const cid = typeof msg.cid === 'string' ? msg.cid : undefined;
    if (!cid) return;
    const h = this.channels.get(cid);
    if (h) fn(h);
  }

  // ─────────────────────────── Keepalive ───────────────────────────────────

  private startKeepalive(): void {
    this.pingTimer = setInterval(() => {
      if (this.closed) return;
      // Send both an app-level ping (so the C++ side can answer in-band) and a
      // ws-level ping (so we catch a dead TCP even if the agent's JSON loop
      // wedges). Either pong re-arms the deadline.
      this.send({ t: 'ping' });
      try {
        this.ws.ping();
      } catch {
        /* socket may be mid-teardown */
      }
      this.armPongDeadline();
    }, this.cfg.pingIntervalMs);
    this.pingTimer.unref?.();
  }

  /** (Re)start the "no pong" countdown; firing it terminates the tunnel. */
  private armPongDeadline(): void {
    if (this.closed) return;
    if (this.pongDeadline) clearTimeout(this.pongDeadline);
    this.pongDeadline = setTimeout(() => {
      log.warn('agent missed pong, dropping tunnel', { home_id: this.homeId });
      this.terminate('pong_timeout');
    }, this.cfg.pongTimeoutMs);
    this.pongDeadline.unref?.();
  }

  // ─────────────────────────── Lifecycle ───────────────────────────────────

  private send(msg: RelayToAgent): void {
    if (this.closed) return;
    try {
      this.ws.send(JSON.stringify(msg));
    } catch (err) {
      log.warn('failed to send to agent', { home_id: this.homeId, error: String(err) });
    }
  }

  /** Force-close the underlying socket; cleanup runs via the 'close' handler. */
  terminate(reason: string): void {
    if (this.closed) return;
    log.info('terminating tunnel', { home_id: this.homeId, reason });
    try {
      this.ws.close(1000, reason.slice(0, 120));
    } catch {
      /* ignore */
    }
    // Guarantee teardown even if 'close' never fires (e.g. half-open socket).
    this.handleSocketClosed(1000, reason);
  }

  /** Invoked once the socket is gone. Idempotent. Rejects/closes everything. */
  private handleSocketClosed(code: number, reason: string): void {
    if (this.closed) return;
    this.closed = true;

    if (this.pingTimer) clearTimeout(this.pingTimer);
    if (this.pongDeadline) clearTimeout(this.pongDeadline);
    this.pingTimer = null;
    this.pongDeadline = null;

    // Fail every in-flight REST request (proxy.ts maps to 502).
    for (const [id, waiter] of this.pending) {
      clearTimeout(waiter.timer);
      waiter.reject(new Error('tunnel_closed'));
      this.pending.delete(id);
    }

    // Close every bridged WS channel (proxy.ts closes the client socket).
    for (const [cid, h] of this.channels) {
      this.channels.delete(cid);
      try {
        h.onClose(1011, 'tunnel_closed');
      } catch {
        /* ignore handler errors during teardown */
      }
    }

    log.info('tunnel closed', { home_id: this.homeId, code, reason });
    this.onClosed?.();
  }
}

/**
 * TunnelManager — registry of live agent tunnels, one per home_id.
 */
export class TunnelManager {
  private readonly tunnels = new Map<string, AgentTunnel>();

  constructor(
    private readonly cfg: Config,
    private readonly registry: Registry,
  ) {}

  /** Look up a live tunnel for routing client traffic. */
  get(homeId: string): AgentTunnel | undefined {
    return this.tunnels.get(homeId);
  }

  /** Number of live tunnels (for /healthz, metrics). */
  get size(): number {
    return this.tunnels.size;
  }

  /**
   * Drive a freshly-accepted agent socket through the hello handshake. On
   * success the socket becomes the live tunnel for its home_id (replacing any
   * previous one). On failure the socket is closed with a reason.
   *
   * We attach a one-shot 'message' listener for the hello, then hand the socket
   * to AgentTunnel which installs its own permanent listeners.
   */
  handleAgentSocket(ws: WebSocket): void {
    let settled = false;

    // Guard: the agent must say hello promptly or we drop the socket.
    const helloTimer = setTimeout(() => {
      if (settled) return;
      settled = true;
      log.warn('agent did not send hello in time');
      try {
        ws.close(4001, 'hello timeout');
      } catch {
        /* ignore */
      }
    }, this.cfg.requestTimeoutMs);
    helloTimer.unref?.();

    const onHello = (data: unknown): void => {
      if (settled) return;

      const raw =
        typeof data === 'string'
          ? data
          : Buffer.isBuffer(data)
            ? data.toString('utf8')
            : String(data);
      const msg = parseFrame(raw);

      if (!msg || msg.t !== 'hello') {
        settled = true;
        clearTimeout(helloTimer);
        ws.off('message', onHello);
        log.warn('first agent frame was not a hello', { t: msg ? String(msg.t) : 'parse_error' });
        try {
          ws.close(4002, 'expected hello');
        } catch {
          /* ignore */
        }
        return;
      }

      const homeId = typeof msg.home_id === 'string' ? msg.home_id : '';
      const secret = typeof msg.secret === 'string' ? msg.secret : '';
      const auth = this.registry.authenticate(homeId, secret);

      if (!auth.ok) {
        settled = true;
        clearTimeout(helloTimer);
        ws.off('message', onHello);
        log.warn('agent auth failed', { home_id: homeId, reason: auth.reason });
        try {
          // 4401 ≈ application "unauthorized" close code.
          ws.close(4401, auth.reason);
        } catch {
          /* ignore */
        }
        return;
      }

      // Authenticated. Promote to a real tunnel.
      settled = true;
      clearTimeout(helloTimer);
      ws.off('message', onHello);

      // One live tunnel per home: evict any predecessor.
      const prev = this.tunnels.get(homeId);
      if (prev) {
        log.info('replacing existing tunnel for home', { home_id: homeId });
        prev.terminate('replaced_by_reconnect');
      }

      const tunnel = new AgentTunnel(ws, homeId, this.cfg);
      tunnel.onClosed = () => {
        // Only delete if we're still the registered tunnel (a newer reconnect
        // may have already replaced us).
        if (this.tunnels.get(homeId) === tunnel) {
          this.tunnels.delete(homeId);
        }
      };
      this.tunnels.set(homeId, tunnel);

      // Acknowledge.
      ws.send(JSON.stringify({ t: 'hello_ok', now: Date.now() }));
      log.info('agent tunnel established', { home_id: homeId, totalTunnels: this.tunnels.size });
    };

    ws.on('message', onHello);
    ws.on('error', (err) => {
      if (settled) return;
      settled = true;
      clearTimeout(helloTimer);
      log.warn('agent socket error before hello', { error: String(err) });
    });
    ws.on('close', () => {
      if (settled) return;
      settled = true;
      clearTimeout(helloTimer);
    });
  }

  /** Tear down every tunnel (graceful shutdown). */
  closeAll(reason: string): void {
    for (const tunnel of this.tunnels.values()) {
      tunnel.terminate(reason);
    }
    this.tunnels.clear();
  }
}
