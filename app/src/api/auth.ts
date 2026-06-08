//
// Pairing + device-token lifecycle. The token is the *only* thing that grants
// access; clearing it (or the desktop revoking the device) fully locks out the
// phone. See docs/REMOTE_ACCESS.md §3.
//
import { ENDPOINTS } from './contract';
import type { PairingPayload, PairRequest, PairResponse } from './contract';
import { getItem, setItem, removeItem } from './storage';
import { relayHttp, saveConnection } from './transport';

const TOKEN_KEY = 'pm.token';
const DEVICE_KEY = 'pm.device';
let token: string | null = null;

export async function loadToken(): Promise<string | null> {
  if (token) return token;
  token = await getItem(TOKEN_KEY);
  return token;
}

export function getTokenSync(): string | null {
  return token;
}

export async function isPaired(): Promise<boolean> {
  return !!(await loadToken());
}

/** Parse a scanned QR — either raw JSON or a `polymath://pair?…` deep link. */
export function parsePairingQR(text: string): PairingPayload {
  const trimmed = text.trim();
  if (trimmed.startsWith('polymath://')) {
    const u = new URL(trimmed);
    const port = u.searchParams.get('port');
    return {
      relay_url: u.searchParams.get('relay') ?? '',
      home_id: u.searchParams.get('home') ?? '',
      pair_code: u.searchParams.get('code') ?? '',
      lan_host: u.searchParams.get('lan') ?? undefined,
      lan_port: port ? Number(port) : undefined,
    };
  }
  return JSON.parse(trimmed) as PairingPayload;
}

function pairingBases(p: PairingPayload): string[] {
  // Try LAN first (we're usually pairing at home), then the relay.
  const bases: string[] = [];
  if (p.lan_host) bases.push(`http://${p.lan_host}:${p.lan_port ?? 8765}`);
  if (p.relay_url) bases.push(relayHttp(p.relay_url, p.home_id));
  return bases;
}

export async function pair(
  payload: PairingPayload,
  deviceName: string,
  platform: 'ios' | 'android' | 'web',
): Promise<PairResponse> {
  const body: PairRequest = {
    code: payload.pair_code,
    device_name: deviceName,
    platform,
  };

  let lastErr: unknown = new Error('no reachable endpoint');
  for (const base of pairingBases(payload)) {
    try {
      const r = await fetch(`${base}${ENDPOINTS.pair}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });
      if (!r.ok) {
        lastErr = new Error(`pairing rejected (${r.status})`);
        continue;
      }
      const res = (await r.json()) as PairResponse;
      token = res.token;
      await setItem(TOKEN_KEY, res.token);
      await setItem(DEVICE_KEY, res.device_id);
      await saveConnection({
        homeId: res.home_id,
        relayUrl: res.relay_url,
        lanHost: payload.lan_host ?? 'polymath.local',
        lanPort: payload.lan_port ?? 8765,
      });
      return res;
    } catch (e) {
      lastErr = e;
    }
  }
  throw lastErr;
}

export async function unpair(): Promise<void> {
  token = null;
  await removeItem(TOKEN_KEY);
  await removeItem(DEVICE_KEY);
}
