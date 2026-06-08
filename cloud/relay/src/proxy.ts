//
// proxy.ts — the CLIENT-facing side of the relay.
//
// A phone talks to the relay as though it were the home server:
//
//   REST:  https://<relay>/h/:home_id/api/...            → handleHttp()
//   WS:    wss://<relay>/h/:home_id/api/v1/events?token= → bridgeWs()
//
// Both are forwarded over the single agent tunnel the home gateway holds open
// (see tunnel.ts). This module:
//   - buffers a client request body (bounded), forwards it, awaits the agent's
//     response, and writes it back; mapping tunnel failures to 502/503/504;
//   - bridges a client WebSocket to an upstream channel over the tunnel,
//     forwarding text frames each way and tearing both sides down together;
//   - does light per-home token-bucket rate limiting.
//
// PRIVACY: bodies and the Authorization header are forwarded UNTOUCHED and are
// never logged. The relay does not validate device tokens — the gateway does
// that end-to-end. We only ever log routing metadata (home_id, method, path,
// status, ids).
//

import type { IncomingMessage, ServerResponse } from 'node:http';

import type { WebSocket } from 'ws';

import type { Config } from './config.js';
import { log } from './log.js';
import type { AgentTunnel, ChannelHandlers, TunnelManager } from './tunnel.js';

// Hop-by-hop headers (RFC 7230 §6.1) must not be forwarded through a proxy.
// We also drop framing headers we (re)compute ourselves on the receiving end.
const HOP_BY_HOP = new Set([
  'connection',
  'keep-alive',
  'proxy-authenticate',
  'proxy-authorization',
  'te',
  'trailer',
  'transfer-encoding',
  'upgrade',
  // Recomputed/owned by the transport, not meaningful to tunnel verbatim:
  'host',
  'content-length',
]);

/** Flatten Node's header bag to a plain string map, dropping hop-by-hop ones. */
function flattenHeaders(raw: IncomingMessage['headers']): Record<string, string> {
  const out: Record<string, string> = {};
  for (const [k, v] of Object.entries(raw)) {
    if (v === undefined) continue;
    const key = k.toLowerCase();
    if (HOP_BY_HOP.has(key)) continue;
    // Node joins duplicate headers (except set-cookie) with ", " already when
    // given as a string; arrays (e.g. set-cookie) we join explicitly.
    out[key] = Array.isArray(v) ? v.join(', ') : v;
  }
  return out;
}

/** Strip hop-by-hop headers from an agent's response before writing it back. */
function sanitizeResponseHeaders(raw: Record<string, string>): Record<string, string> {
  const out: Record<string, string> = {};
  for (const [k, v] of Object.entries(raw)) {
    const key = k.toLowerCase();
    if (HOP_BY_HOP.has(key)) continue;
    out[key] = v;
  }
  return out;
}

// ───────────────────────── Rate limiting ───────────────────────────────────

/**
 * One token bucket per home_id. Cheap, allocation-light, and self-cleaning:
 * buckets are created lazily and only the homes seeing traffic hold state.
 * Buckets refill continuously at ratePerSec up to `burst`.
 */
class RateLimiter {
  private readonly buckets = new Map<string, { tokens: number; last: number }>();

  constructor(private readonly ratePerSec: number, private readonly burst: number) {}

  /** Try to spend one token for `homeId`. Returns true if allowed. */
  allow(homeId: string): boolean {
    // A non-positive rate disables limiting entirely.
    if (this.ratePerSec <= 0) return true;

    const now = Date.now();
    let b = this.buckets.get(homeId);
    if (!b) {
      b = { tokens: this.burst, last: now };
      this.buckets.set(homeId, b);
    }
    // Refill based on elapsed time.
    const elapsedSec = (now - b.last) / 1000;
    if (elapsedSec > 0) {
      b.tokens = Math.min(this.burst, b.tokens + elapsedSec * this.ratePerSec);
      b.last = now;
    }
    if (b.tokens >= 1) {
      b.tokens -= 1;
      return true;
    }
    return false;
  }
}

// ───────────────────────── Small helpers ───────────────────────────────────

/** Write a tiny JSON error to a client whose request never reached the home. */
function writeError(res: ServerResponse, status: number, code: string): void {
  const body = Buffer.from(JSON.stringify({ error: code }), 'utf8');
  res.writeHead(status, {
    'content-type': 'application/json',
    'content-length': String(body.length),
  });
  res.end(body);
}

/**
 * Read a client request body into a single Buffer, enforcing a byte cap.
 * Resolves null on an over-limit body (caller replies 413). Rejects on a
 * transport read error.
 */
function readBody(req: IncomingMessage, maxBytes: number): Promise<Buffer | null> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    let total = 0;
    let done = false;

    const finish = (value: Buffer | null): void => {
      if (done) return;
      done = true;
      req.removeListener('data', onData);
      req.removeListener('end', onEnd);
      req.removeListener('error', onErr);
      resolve(value);
    };

    const onData = (chunk: Buffer): void => {
      if (done) return;
      total += chunk.length;
      if (total > maxBytes) {
        // Stop accumulating; drain is the server's problem (we'll close).
        finish(null);
        return;
      }
      chunks.push(chunk);
    };
    const onEnd = (): void => finish(Buffer.concat(chunks));
    const onErr = (err: Error): void => {
      if (done) return;
      done = true;
      req.removeListener('data', onData);
      req.removeListener('end', onEnd);
      reject(err);
    };

    req.on('data', onData);
    req.on('end', onEnd);
    req.on('error', onErr);
  });
}

// ───────────────────────── Proxy ───────────────────────────────────────────

export class Proxy {
  private readonly limiter: RateLimiter;

  constructor(
    private readonly tunnels: TunnelManager,
    private readonly cfg: Config,
  ) {
    this.limiter = new RateLimiter(cfg.rateLimit.ratePerSec, cfg.rateLimit.burst);
  }

  /**
   * Handle an inbound REST request to /h/:home_id/<subPath>.
   *
   * @param subPath path AFTER /h/:home_id, INCLUDING the query string, exactly
   *                as the gateway expects to see it (e.g. "/api/v1/status?x=1").
   */
  async handleHttp(
    req: IncomingMessage,
    res: ServerResponse,
    homeId: string,
    subPath: string,
  ): Promise<void> {
    const method = req.method ?? 'GET';

    if (!this.limiter.allow(homeId)) {
      log.warn('rate limited', { home_id: homeId, method, path: subPath });
      writeError(res, 429, 'rate_limited');
      return;
    }

    const tunnel = this.tunnels.get(homeId);
    if (!tunnel) {
      // The home isn't connected to us right now.
      log.info('no tunnel for home', { home_id: homeId, method, path: subPath });
      writeError(res, 503, 'home_offline');
      return;
    }

    // Buffer the request body (bounded).
    let body: Buffer | null;
    try {
      body = await readBody(req, this.cfg.maxBodyBytes);
    } catch (err) {
      log.warn('error reading client body', { home_id: homeId, error: String(err) });
      writeError(res, 400, 'bad_request_body');
      return;
    }
    if (body === null) {
      writeError(res, 413, 'payload_too_large');
      // The connection is in an indeterminate read state; close it.
      req.destroy();
      return;
    }

    const headers = flattenHeaders(req.headers);

    try {
      const r = await tunnel.request(method, subPath, headers, body);
      const outHeaders = sanitizeResponseHeaders(r.headers);
      // Always set an accurate content-length for the bytes we actually write.
      outHeaders['content-length'] = String(r.body.length);
      res.writeHead(r.status, outHeaders);
      res.end(r.body);
      log.info('proxied', { home_id: homeId, method, path: subPath, status: r.status });
    } catch (err) {
      const reason = err instanceof Error ? err.message : String(err);
      // Distinguish a timeout (home is up but slow / silent) from a dropped
      // tunnel (home vanished mid-request).
      const status = reason === 'timeout' ? 504 : 502;
      log.warn('proxy failed', { home_id: homeId, method, path: subPath, reason });
      if (!res.headersSent) {
        writeError(res, status, reason === 'timeout' ? 'gateway_timeout' : 'bad_gateway');
      } else {
        res.destroy();
      }
    }
  }

  /**
   * Bridge an already-upgraded client WebSocket to an upstream channel over the
   * tunnel. Forwards text frames both ways and closes both sides together.
   *
   * @param client  the accepted client-side WebSocket (from WebSocketServer).
   * @param homeId  target home.
   * @param subPath path AFTER /h/:home_id, WITHOUT the query string.
   * @param query   raw query string WITHOUT a leading "?" (may be "").
   * @param headers flattened upgrade-request headers (forwarded untouched).
   */
  bridgeWs(
    client: WebSocket,
    homeId: string,
    subPath: string,
    query: string,
    headers: Record<string, string>,
  ): void {
    if (!this.limiter.allow(homeId)) {
      log.warn('ws rate limited', { home_id: homeId, path: subPath });
      try {
        client.close(1013, 'rate_limited'); // 1013 = "try again later"
      } catch {
        /* ignore */
      }
      return;
    }

    const tunnel: AgentTunnel | undefined = this.tunnels.get(homeId);
    if (!tunnel) {
      log.info('ws but no tunnel for home', { home_id: homeId, path: subPath });
      try {
        client.close(1011, 'home_offline'); // 1011 = server can't fulfil
      } catch {
        /* ignore */
      }
      return;
    }

    let cid: string | null = null;
    let opened = false;
    let closed = false;

    // Frames the client sends before upstream confirms `ws_open_ok` are queued
    // so we don't lose an early subscribe/ping.
    const preOpenQueue: string[] = [];

    const teardown = (code?: number, reason?: string): void => {
      if (closed) return;
      closed = true;
      // Tell the agent to drop the upstream side (if we ever opened it).
      if (cid) tunnel.closeChannel(cid, code, reason);
      try {
        if (client.readyState === client.OPEN || client.readyState === client.CONNECTING) {
          client.close(code ?? 1000, (reason ?? '').slice(0, 120));
        }
      } catch {
        /* ignore */
      }
      log.info('ws bridge closed', { home_id: homeId, path: subPath, ...(reason ? { reason } : {}) });
    };

    const handlers: ChannelHandlers = {
      onOpenOk: () => {
        if (closed) return;
        opened = true;
        // Flush anything the client sent while we were still opening.
        for (const data of preOpenQueue) {
          if (cid) tunnel.sendChannelMessage(cid, data);
        }
        preOpenQueue.length = 0;
        log.info('ws bridge open', { home_id: homeId, path: subPath });
      },
      onError: (error: string) => {
        if (closed) return;
        log.warn('ws upstream error', { home_id: homeId, path: subPath, error });
        // 1011: the upstream couldn't be established.
        closed = true; // prevent teardown() from re-sending a ws_close for cid
        try {
          client.close(1011, 'upstream_error');
        } catch {
          /* ignore */
        }
      },
      onMessage: (data: string) => {
        if (closed) return;
        try {
          client.send(data);
        } catch (err) {
          log.warn('failed to deliver upstream frame to client', {
            home_id: homeId,
            error: String(err),
          });
        }
      },
      onClose: (code?: number, reason?: string) => {
        // Upstream (or the whole tunnel) closed this channel.
        if (closed) return;
        closed = true;
        try {
          client.close(code ?? 1000, (reason ?? '').slice(0, 120));
        } catch {
          /* ignore */
        }
      },
    };

    cid = tunnel.openChannel(subPath, query, headers, handlers);
    if (!cid) {
      // Tunnel went away between the lookup and the open.
      try {
        client.close(1011, 'home_offline');
      } catch {
        /* ignore */
      }
      return;
    }

    // Client → upstream.
    client.on('message', (data: unknown, isBinary: boolean) => {
      if (closed) return;
      // The Polymath events protocol is JSON text. Coerce to a UTF-8 string;
      // binary client frames are unexpected but forwarded as text best-effort.
      const text = toText(data, isBinary);
      if (text === null) return;
      if (opened) {
        tunnel.sendChannelMessage(cid as string, text);
      } else {
        // Bound the pre-open queue so a misbehaving client can't grow it without
        // limit before the channel is ready.
        if (preOpenQueue.length < 64) preOpenQueue.push(text);
      }
    });

    client.on('close', (code: number, reason: Buffer) => {
      teardown(code, reason.toString());
    });

    client.on('error', (err: Error) => {
      log.warn('client ws error', { home_id: homeId, error: String(err) });
      teardown(1011, 'client_error');
    });
  }
}

/** Coerce a ws message payload to a UTF-8 string, or null if uninterpretable. */
function toText(data: unknown, _isBinary: boolean): string | null {
  if (typeof data === 'string') return data;
  if (Buffer.isBuffer(data)) return data.toString('utf8');
  if (Array.isArray(data)) {
    try {
      return Buffer.concat(data as Buffer[]).toString('utf8');
    } catch {
      return null;
    }
  }
  if (data instanceof ArrayBuffer) return Buffer.from(data).toString('utf8');
  return null;
}
