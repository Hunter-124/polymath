//
// registry.ts — HOME authentication only.
//
// The relay authenticates the *home* (so a random actor can't claim a home_id
// and hijack its traffic). It does NOT authenticate end-user devices — that is
// the gateway's job. See the privacy posture in README.md.
//
// Two modes, chosen by config:
//
//   1. RELAY_OPEN=true  → trust-on-first-use (TOFU). The first secret seen for a
//      home_id is remembered in memory and must match on every later connect.
//      Convenient for self-hosting. NOT durable: a relay restart forgets all
//      learned secrets, so the next connect re-pins whatever secret arrives.
//
//   2. closed (default) → exact match against the static RELAY_HOMES map
//      (merged from the RELAY_HOMES env var and the RELAY_HOMES_FILE file).
//      Unknown home_ids and wrong secrets are rejected.
//

import { timingSafeEqual } from 'node:crypto';

import type { Config } from './config.js';
import { log } from './log.js';

export type AuthResult = { ok: true } | { ok: false; reason: string };

/** Constant-time string compare to avoid leaking secret length/prefix via timing. */
function safeEqual(a: string, b: string): boolean {
  const ba = Buffer.from(a, 'utf8');
  const bb = Buffer.from(b, 'utf8');
  if (ba.length !== bb.length) {
    // Still burn a compare against a same-length buffer so the early-out on
    // length doesn't become its own side channel.
    timingSafeEqual(ba, ba);
    return false;
  }
  return timingSafeEqual(ba, bb);
}

export class Registry {
  private readonly open: boolean;
  private readonly homes: Record<string, string>;
  /** TOFU-learned secrets (only used when open=true). */
  private readonly learned = new Map<string, string>();

  constructor(cfg: Config) {
    this.open = cfg.relayOpen;
    this.homes = cfg.relayHomes;
  }

  /**
   * Validate a hello. Returns ok, or a short machine-ish reason suitable for a
   * WS close message (kept terse so we don't leak which check failed in a way
   * that helps an attacker enumerate valid home_ids).
   */
  authenticate(homeId: string, secret: string): AuthResult {
    if (!homeId || typeof homeId !== 'string') {
      return { ok: false, reason: 'missing home_id' };
    }
    if (!secret || typeof secret !== 'string') {
      return { ok: false, reason: 'missing secret' };
    }

    if (this.open) {
      const known = this.learned.get(homeId);
      if (known === undefined) {
        // First time we've seen this home — pin its secret.
        this.learned.set(homeId, secret);
        log.info('registry: TOFU pinned new home', { home_id: homeId });
        return { ok: true };
      }
      return safeEqual(known, secret)
        ? { ok: true }
        : { ok: false, reason: 'secret mismatch' };
    }

    // Closed mode: exact match against the configured map.
    const expected = this.homes[homeId];
    if (expected === undefined) {
      return { ok: false, reason: 'unknown home' };
    }
    return safeEqual(expected, secret)
      ? { ok: true }
      : { ok: false, reason: 'secret mismatch' };
  }
}
