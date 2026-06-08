//
// config.ts — environment parsing, in one place.
//
// Everything tunable lives here so the rest of the code reads a typed object
// instead of poking at process.env. Values are read once at startup.
//

import { readFileSync } from 'node:fs';

import { log } from './log.js';

export interface Config {
  /** TCP port the public HTTP/WS server listens on. */
  port: number;

  /**
   * Trust-on-first-use home auth. When true, the first secret presented for a
   * given home_id is remembered (in memory) and required to match thereafter.
   * Convenient for self-hosting; NOT durable across restarts.
   */
  relayOpen: boolean;

  /**
   * Static home → secret map. Merged from two sources:
   *   - the RELAY_HOMES env var (JSON: { "<home_id>": "<secret>", ... }), and
   *   - a JSON file at the path in RELAY_HOMES_FILE (same shape).
   * The env var wins on key collisions (handy for one-off overrides). Used only
   * when relayOpen is false. Empty map + closed mode ⇒ nobody can register,
   * which is a safe (if useless) default.
   */
  relayHomes: Record<string, string>;

  /** How long the relay waits for an agent `res` before returning 504 (ms). */
  requestTimeoutMs: number;

  /**
   * Idle interval after which the relay sends an app-level `ping` to an agent
   * (ms). If no `pong` (or ws-level pong) arrives within pongTimeoutMs, the
   * tunnel is dropped.
   */
  pingIntervalMs: number;
  pongTimeoutMs: number;

  /** Max client request body the relay will buffer before 413 (bytes). */
  maxBodyBytes: number;

  /** Per-home token-bucket rate limit for inbound client requests. */
  rateLimit: {
    /** Sustained requests/second allowed per home_id. */
    ratePerSec: number;
    /** Bucket capacity (burst). */
    burst: number;
  };
}

function num(name: string, def: number): number {
  const raw = process.env[name];
  if (raw === undefined || raw === '') return def;
  const n = Number(raw);
  if (!Number.isFinite(n)) {
    log.warn('invalid numeric env, using default', { name, raw, def });
    return def;
  }
  return n;
}

function bool(name: string, def: boolean): boolean {
  const raw = process.env[name];
  if (raw === undefined) return def;
  return /^(1|true|yes|on)$/i.test(raw.trim());
}

/** Validate a parsed JSON value as a flat home_id → secret string map. */
function asHomesMap(parsed: unknown, source: string): Record<string, string> {
  if (parsed === null || typeof parsed !== 'object' || Array.isArray(parsed)) {
    throw new Error(`${source} must be a JSON object of home_id → secret`);
  }
  const out: Record<string, string> = {};
  for (const [k, v] of Object.entries(parsed as Record<string, unknown>)) {
    if (typeof v !== 'string') {
      throw new Error(`${source}["${k}"] must be a string secret`);
    }
    out[k] = v;
  }
  return out;
}

function parseHomesEnv(raw: string | undefined): Record<string, string> {
  if (!raw || raw.trim() === '') return {};
  try {
    return asHomesMap(JSON.parse(raw), 'RELAY_HOMES');
  } catch (err) {
    // Fail loud: a typo here silently locks everyone out otherwise.
    log.error('failed to parse RELAY_HOMES', { error: String(err) });
    throw err;
  }
}

function parseHomesFile(path: string | undefined): Record<string, string> {
  if (!path || path.trim() === '') return {};
  let text: string;
  try {
    text = readFileSync(path, 'utf8');
  } catch (err) {
    log.error('failed to read RELAY_HOMES_FILE', { path, error: String(err) });
    throw err;
  }
  try {
    return asHomesMap(JSON.parse(text), `RELAY_HOMES_FILE (${path})`);
  } catch (err) {
    log.error('failed to parse RELAY_HOMES_FILE', { path, error: String(err) });
    throw err;
  }
}

export function loadConfig(): Config {
  const relayOpen = bool('RELAY_OPEN', false);

  // File first, then env — env overrides file on key collision.
  const fileHomes = parseHomesFile(process.env.RELAY_HOMES_FILE);
  const envHomes = parseHomesEnv(process.env.RELAY_HOMES);
  const relayHomes = { ...fileHomes, ...envHomes };

  const cfg: Config = {
    port: num('PORT', 8080),
    relayOpen,
    relayHomes,
    requestTimeoutMs: num('REQUEST_TIMEOUT_MS', 30_000),
    pingIntervalMs: num('PING_INTERVAL_MS', 20_000),
    pongTimeoutMs: num('PONG_TIMEOUT_MS', 10_000),
    maxBodyBytes: num('MAX_BODY_BYTES', 10 * 1024 * 1024), // 10 MiB
    rateLimit: {
      ratePerSec: num('RATE_LIMIT_PER_SEC', 50),
      burst: num('RATE_LIMIT_BURST', 100),
    },
  };

  log.info('config loaded', {
    port: cfg.port,
    relayOpen: cfg.relayOpen,
    knownHomes: Object.keys(cfg.relayHomes).length,
    requestTimeoutMs: cfg.requestTimeoutMs,
    maxBodyBytes: cfg.maxBodyBytes,
  });

  if (cfg.relayOpen) {
    log.warn(
      'RELAY_OPEN=true — trust-on-first-use home auth is ON. The first secret ' +
        'presented for each home_id is accepted and pinned in memory until ' +
        'restart. Fine for self-hosting; do NOT use on a shared/public relay.',
    );
  }

  if (!cfg.relayOpen && Object.keys(cfg.relayHomes).length === 0) {
    log.warn(
      'RELAY_OPEN is false and no homes are configured — no home can authenticate. ' +
        'Set RELAY_OPEN=true for trust-on-first-use, or provide RELAY_HOMES JSON ' +
        'or a RELAY_HOMES_FILE.',
    );
  }

  return cfg;
}
