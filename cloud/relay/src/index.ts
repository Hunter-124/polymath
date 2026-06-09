//
// index.ts — the relay entrypoint.
//
// Wires together:
//   - a plain Node HTTP server for client REST + health checks,
//   - a `ws` WebSocketServer in noServer mode so we own the `upgrade` routing,
//   - the TunnelManager (agent side) and Proxy (client side).
//
// Routing
// ───────
//   HTTP
//     GET  /healthz            → 200 (liveness; reports live tunnel count)
//     ANY  /h/:home_id/<path>  → Proxy.handleHttp (forwarded over the tunnel)
//     else                     → 404
//
//   WS upgrade
//     /agent                   → TunnelManager.handleAgentSocket (home dials in)
//     /h/:home_id/<path>       → Proxy.bridgeWs (phone's events socket, etc.)
//     else                     → 404 + socket destroyed
//
// The home gateway is the only party that connects to /agent; everyone else is
// a client reaching a home by its opaque home_id.
//

import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';
import type { Duplex } from 'node:stream';

import { WebSocketServer, type WebSocket } from 'ws';

import { loadConfig } from './config.js';
import { log } from './log.js';
import { Proxy } from './proxy.js';
import { Registry } from './registry.js';
import { TunnelManager } from './tunnel.js';

/** Parsed `/h/:home_id/<rest>` route. */
interface HomeRoute {
  homeId: string;
  /** Everything after /h/:home_id, WITH leading "/", WITHOUT query. e.g. "/api/v1/status". */
  path: string;
  /** Raw query string WITHOUT the leading "?". May be "". */
  query: string;
}

/**
 * Match a raw request URL against `/h/:home_id/...`. Returns null if it isn't a
 * home route. We slice the raw string (rather than re-encoding via URL) so the
 * path + query reach the gateway byte-for-byte as the client sent them.
 */
function matchHomeRoute(rawUrl: string): HomeRoute | null {
  // Split off the query first; keep it verbatim.
  const qIdx = rawUrl.indexOf('?');
  const pathname = qIdx === -1 ? rawUrl : rawUrl.slice(0, qIdx);
  const query = qIdx === -1 ? '' : rawUrl.slice(qIdx + 1);

  // Expect "/h/<home_id>" optionally followed by "/<rest>".
  if (!pathname.startsWith('/h/')) return null;
  const afterPrefix = pathname.slice('/h/'.length); // "<home_id>/rest..." or "<home_id>"
  if (afterPrefix.length === 0) return null;

  const slash = afterPrefix.indexOf('/');
  const rawHomeId = slash === -1 ? afterPrefix : afterPrefix.slice(0, slash);
  if (rawHomeId.length === 0) return null;

  // home_id may be percent-encoded in the URL; the gateway keys on the decoded
  // value. Fall back to the raw token if it isn't valid encoding.
  let homeId: string;
  try {
    homeId = decodeURIComponent(rawHomeId);
  } catch {
    homeId = rawHomeId;
  }

  // The sub-path is whatever followed the home id, normalised to start with "/".
  const sub = slash === -1 ? '' : afterPrefix.slice(slash); // "/rest..." or ""
  const path = sub.length === 0 ? '/' : sub;

  return { homeId, path, query };
}

function main(): void {
  const cfg = loadConfig();
  const registry = new Registry(cfg);
  const tunnels = new TunnelManager(cfg, registry);
  const proxy = new Proxy(tunnels, cfg);

  // noServer: we route `upgrade` ourselves and only call handleUpgrade for the
  // paths we recognise, so a stray upgrade can't accidentally complete.
  const wss = new WebSocketServer({ noServer: true, maxPayload: cfg.maxBodyBytes });

  const server = createServer((req: IncomingMessage, res: ServerResponse) => {
    const url = req.url ?? '/';

    // Liveness / readiness probe. No app data; just confirms the process is up.
    if (req.method === 'GET' && (url === '/healthz' || url === '/healthz/')) {
      const payload = Buffer.from(
        JSON.stringify({ ok: true, tunnels: tunnels.size }),
        'utf8',
      );
      res.writeHead(200, {
        'content-type': 'application/json',
        'content-length': String(payload.length),
      });
      res.end(payload);
      return;
    }

    const route = matchHomeRoute(url);
    if (route) {
      // handleHttp wants the sub-path WITH its query string appended.
      const fullPath = route.query ? `${route.path}?${route.query}` : route.path;
      void proxy.handleHttp(req, res, route.homeId, fullPath);
      return;
    }

    // Anything else is not part of the relay surface.
    const body = Buffer.from(JSON.stringify({ error: 'not_found' }), 'utf8');
    res.writeHead(404, {
      'content-type': 'application/json',
      'content-length': String(body.length),
    });
    res.end(body);
  });

  // ───────────────────────── WS upgrade routing ────────────────────────────

  server.on('upgrade', (req: IncomingMessage, socket: Duplex, head: Buffer) => {
    const url = req.url ?? '/';

    // The agent (home gateway) tunnel.
    if (url === '/agent') {
      wss.handleUpgrade(req, socket, head, (ws: WebSocket) => {
        tunnels.handleAgentSocket(ws);
      });
      return;
    }

    // A client WebSocket bound for a home (e.g. the events stream).
    const route = matchHomeRoute(url);
    if (route) {
      const headers = flattenUpgradeHeaders(req);
      wss.handleUpgrade(req, socket, head, (ws: WebSocket) => {
        proxy.bridgeWs(ws, route.homeId, route.path, route.query, headers);
      });
      return;
    }

    // Unknown upgrade target — refuse cleanly.
    socket.write('HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n');
    socket.destroy();
  });

  // A server-level error on the listener (rare) shouldn't crash silently.
  server.on('error', (err) => {
    log.error('http server error', { error: String(err) });
  });

  server.listen(cfg.port, () => {
    log.info('relay listening', { port: cfg.port });
  });

  // ───────────────────────── Graceful shutdown ─────────────────────────────

  let shuttingDown = false;
  const shutdown = (signal: string): void => {
    if (shuttingDown) return;
    shuttingDown = true;
    log.info('shutting down', { signal });

    // Stop accepting new connections, drop tunnels, then exit.
    tunnels.closeAll('relay_shutdown');
    wss.close();
    server.close(() => {
      log.info('shutdown complete');
      process.exit(0);
    });

    // Don't hang forever if a socket refuses to close.
    const force = setTimeout(() => {
      log.warn('forced exit after shutdown timeout');
      process.exit(0);
    }, 5_000);
    force.unref?.();
  };

  process.on('SIGTERM', () => shutdown('SIGTERM'));
  process.on('SIGINT', () => shutdown('SIGINT'));

  // Last-resort guards so a stray error doesn't take the process down without a
  // trace (it stays up; the relay is a long-lived service).
  process.on('uncaughtException', (err) => {
    log.error('uncaught exception', { error: String(err), stack: (err as Error).stack });
  });
  process.on('unhandledRejection', (reason) => {
    log.error('unhandled rejection', { reason: String(reason) });
  });
}

/** Flatten an upgrade request's headers to a plain string map for forwarding. */
function flattenUpgradeHeaders(req: IncomingMessage): Record<string, string> {
  const out: Record<string, string> = {};
  for (const [k, v] of Object.entries(req.headers)) {
    if (v === undefined) continue;
    const key = k.toLowerCase();
    // Drop the WS handshake mechanics — the gateway opens its OWN upstream
    // socket and will generate fresh handshake headers. We forward the
    // application-meaningful ones (notably Authorization, Cookie, and any
    // Sec-WebSocket-Protocol the app negotiates).
    if (
      key === 'connection' ||
      key === 'upgrade' ||
      key === 'sec-websocket-key' ||
      key === 'sec-websocket-version' ||
      key === 'sec-websocket-accept' ||
      key === 'sec-websocket-extensions' ||
      key === 'host'
    ) {
      continue;
    }
    out[key] = Array.isArray(v) ? v.join(', ') : v;
  }
  return out;
}

main();
