//
// Transport resolution: pick the LAN fast-path when the phone is home, fall back
// to the relay when away. Both expose the identical REST + WS surface, so the
// rest of the app is oblivious to which path is live.
//
//   LAN    →  http(s)://<lanHost>:<lanPort>            (direct to the gateway)
//   relay  →  https://<relay>/h/<homeId>               (reverse-tunnelled)
//
import { Network } from '@capacitor/network';
import { ENDPOINTS } from './contract';
import { getItem, setItem } from './storage';

export interface Connection {
  homeId: string;
  relayUrl: string; // "wss://host"  (empty ⇒ LAN-only)
  lanHost: string; // "polymath.local" or an IP
  lanPort: number;
}

const KEY = 'pm.connection';
let cached: Connection | null = null;
let activeBase: string | null = null;
let lastProbe = 0;
const PROBE_TTL_MS = 15_000;

export async function loadConnection(): Promise<Connection | null> {
  if (cached) return cached;
  const raw = await getItem(KEY);
  cached = raw ? (JSON.parse(raw) as Connection) : null;
  return cached;
}

export async function saveConnection(c: Connection): Promise<void> {
  cached = c;
  activeBase = null; // force re-probe
  await setItem(KEY, JSON.stringify(c));
}

function lanHttp(c: Connection): string {
  return `http://${c.lanHost}:${c.lanPort}`;
}

export function relayHttp(relayUrl: string, homeId: string): string {
  const https = relayUrl
    .replace(/^wss:/, 'https:')
    .replace(/^ws:/, 'http:')
    .replace(/\/$/, '');
  return `${https}/h/${homeId}`;
}

async function reachable(base: string, timeoutMs = 1500): Promise<boolean> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), timeoutMs);
  try {
    const r = await fetch(`${base}${ENDPOINTS.status}`, { signal: ctrl.signal });
    // Any HTTP reply (even 401) proves the server is on the other end.
    return r.ok || r.status === 401;
  } catch {
    return false;
  } finally {
    clearTimeout(timer);
  }
}

/** Resolve the HTTP base URL, caching the choice briefly. */
export async function resolveHttpBase(force = false): Promise<string> {
  const c = await loadConnection();
  if (!c) throw new Error('not_paired');

  const now = Date.now();
  if (!force && activeBase && now - lastProbe < PROBE_TTL_MS) return activeBase;

  let onWifi = true;
  try {
    onWifi = (await Network.getStatus()).connectionType !== 'cellular';
  } catch {
    /* web — assume LAN is worth a try */
  }

  let base: string;
  if (onWifi && c.lanHost && (await reachable(lanHttp(c)))) {
    base = lanHttp(c);
  } else if (c.relayUrl) {
    base = relayHttp(c.relayUrl, c.homeId);
  } else {
    base = lanHttp(c); // LAN-only pairing while away — will fail loudly in UI
  }

  activeBase = base;
  lastProbe = now;
  return base;
}

export async function resolveWsUrl(token: string | null): Promise<string> {
  const base = await resolveHttpBase();
  const ws = base.replace(/^http:/, 'ws:').replace(/^https:/, 'wss:');
  const q = token ? `?token=${encodeURIComponent(token)}` : '';
  return `${ws}${ENDPOINTS.events}${q}`;
}

export function currentBase(): string | null {
  return activeBase;
}

/** True when traffic is going through the relay rather than the LAN. */
export function isRemote(): boolean {
  return !!activeBase && activeBase.includes('/h/');
}

export function invalidateBase(): void {
  activeBase = null;
}
