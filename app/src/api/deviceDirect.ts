//
// Direct-to-device client for edge cameras/instruments on the LAN or SoftAP.
// Completely separate from the gateway transport — authenticates directly with
// HMAC-SHA256 per FABRIC.md §6.
//
// Auth: Authorization: Bearer <base64url(HMAC-SHA256(key, path + "." + ts))>
//       X-Hearth-Ts: <unix seconds>
// The gateway uses the same scheme (src/gateway/auth.h), so one key language.
//
import { getItem, setItem, removeItem } from './storage';

const PAIRED_PREFIX = 'hearth.dev.';

// ─── Stored per-camera pairing ─────────────────────────────────────────────

export interface PairedCamera {
  device_id: string;
  key: string;    // base64-encoded 32-byte shared secret from QR
  lan_host: string; // mDNS name or IP (e.g. "hearth-cam-a1b2c3.local")
  softap?: string;  // fallback SoftAP SSID
}

/** QR payload from FABRIC.md §6. */
export interface CameraQRPayload {
  v: number;
  device_id: string;
  kind: string;
  key: string;
  softap?: string;
  lan_host?: string;
}

export function parseCameraQR(text: string): CameraQRPayload {
  return JSON.parse(text.trim()) as CameraQRPayload;
}

export async function savePairedCamera(c: PairedCamera): Promise<void> {
  await setItem(PAIRED_PREFIX + c.device_id, JSON.stringify(c));
}

export async function loadPairedCamera(device_id: string): Promise<PairedCamera | null> {
  const raw = await getItem(PAIRED_PREFIX + device_id);
  return raw ? (JSON.parse(raw) as PairedCamera) : null;
}

export async function removePairedCamera(device_id: string): Promise<void> {
  await removeItem(PAIRED_PREFIX + device_id);
}

// ─── HMAC-SHA256 bearer ────────────────────────────────────────────────────

/** Import the base64-encoded key bytes for Web Crypto. */
async function importKey(keyB64: string): Promise<CryptoKey> {
  const raw = Uint8Array.from(atob(keyB64), (c) => c.charCodeAt(0));
  return crypto.subtle.importKey(
    'raw',
    raw,
    { name: 'HMAC', hash: 'SHA-256' },
    false,
    ['sign'],
  );
}

/** Build the bearer token: HMAC-SHA256(key, path + "." + ts), base64url-encoded. */
async function makeBearer(keyB64: string, path: string, ts: number): Promise<string> {
  const ck = await importKey(keyB64);
  const msg = new TextEncoder().encode(`${path}.${ts}`);
  const sig = await crypto.subtle.sign('HMAC', ck, msg);
  // base64url (no padding)
  return btoa(String.fromCharCode(...new Uint8Array(sig)))
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '');
}

// ─── HTTP helpers ──────────────────────────────────────────────────────────

export class DeviceApiError extends Error {
  constructor(
    message: string,
    public status: number,
  ) {
    super(message);
  }
}

async function dreq<T>(
  base: string,
  keyB64: string,
  path: string,
  init: RequestInit = {},
): Promise<T> {
  const ts = Math.floor(Date.now() / 1000);
  const bearer = await makeBearer(keyB64, path, ts);
  const headers = new Headers(init.headers);
  headers.set('Authorization', `Bearer ${bearer}`);
  headers.set('X-Hearth-Ts', String(ts));
  if (init.body && !headers.has('Content-Type')) {
    headers.set('Content-Type', 'application/json');
  }

  let r: Response;
  try {
    r = await fetch(`${base}${path}`, { ...init, headers });
  } catch (e) {
    throw new DeviceApiError((e as Error).message || 'network_error', 0);
  }

  if (!r.ok) {
    throw new DeviceApiError(await r.text().catch(() => r.statusText), r.status);
  }
  if (r.status === 204) return undefined as T;
  const ct = r.headers.get('content-type') ?? '';
  return (ct.includes('application/json') ? await r.json() : await r.text()) as T;
}

// ─── Device status shape ───────────────────────────────────────────────────

export interface DeviceStatus {
  device_id: string;
  kind: string;
  name: string;
  fw: string;
  uptime_s: number;
  online: boolean;
}

export interface ClipInfo {
  file: string;
  url: string; // full absolute URL on the device
  size?: number;
  ts?: number;
}

// ─── Per-camera client ─────────────────────────────────────────────────────

/** Resolve the best base URL for a paired camera (LAN or SoftAP). */
function cameraBase(cam: PairedCamera): string {
  // mDNS/IP host; SoftAP fallback is handled at network level (OS connects to it).
  return `http://${cam.lan_host}`;
}

export function makeDeviceClient(cam: PairedCamera) {
  const base = cameraBase(cam);
  const key = cam.key;

  const get = <T>(path: string) => dreq<T>(base, key, path);
  const post = <T>(path: string, body?: unknown) =>
    dreq<T>(base, key, path, {
      method: 'POST',
      body: body != null ? JSON.stringify(body) : undefined,
    });

  return {
    /** GET /status */
    status: () => get<DeviceStatus>('/status'),

    /** Absolute URL for <img src> — the token-bearing auth headers cannot be set
     *  on img tags, so we expose a URL builder that embeds ts+hmac as query params
     *  matching the device's optional ?token= fallback (firmware parity with the
     *  gateway). Callers should use a short-lived URL. */
    snapshotUrl: async (): Promise<string> => {
      const ts = Math.floor(Date.now() / 1000);
      const bearer = await makeBearer(key, '/snapshot', ts);
      return `${base}/snapshot?ts=${ts}&token=${encodeURIComponent(bearer)}`;
    },

    /** Returns the device's MJPEG stream URL with embedded auth params. */
    streamUrl: async (): Promise<string> => {
      const ts = Math.floor(Date.now() / 1000);
      const bearer = await makeBearer(key, '/stream', ts);
      return `${base}/stream?ts=${ts}&token=${encodeURIComponent(bearer)}`;
    },

    /** GET /clips — list of recorded clips */
    clips: () => get<ClipInfo[]>('/clips'),

    /** Absolute clip URL with auth params. */
    clipUrl: async (file: string): Promise<string> => {
      const path = `/clips/${encodeURIComponent(file)}`;
      const ts = Math.floor(Date.now() / 1000);
      const bearer = await makeBearer(key, path, ts);
      return `${base}${path}?ts=${ts}&token=${encodeURIComponent(bearer)}`;
    },

    /** POST /provision — send Wi-Fi credentials during SoftAP setup. */
    provision: (ssid: string, pass: string) =>
      post<void>('/provision', { ssid, pass }),

    /** POST /pair — exchange QR key → device token. */
    pair: (device_name: string) =>
      post<{ token: string }>('/pair', { device_name }),

    /** GET /config */
    getConfig: () => get<Record<string, unknown>>('/config'),

    /** POST /config */
    postConfig: (cfg: Record<string, unknown>) => post<void>('/config', cfg),
  };
}

export type DeviceClient = ReturnType<typeof makeDeviceClient>;
