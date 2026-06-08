//
// protocol.ts — the tunnel wire format (framed JSON over the single agent WS).
//
// This is the SOURCE OF TRUTH for the tunnel protocol. The C++ gateway
// (src/gateway/relay_client) implements the mirror image of these messages, so
// any change here must be reflected there and in cloud/relay/README.md.
//
// All messages are JSON text frames with a discriminant field `t`.
//
// Conventions:
//   - `id`   identifies a single REST request/response pair (relay-assigned).
//   - `cid`  identifies a WebSocket channel being bridged (relay-assigned).
//   - `body` / `data` carrying bytes are base64; `body` may be null when empty.
//   - `path` is everything AFTER `/h/:home_id` (e.g. "/api/v1/status"), so the
//     home gateway sees the same path it would on the LAN.
//

// ───────────────────────── Handshake (client → relay agent) ─────────────────

/** Agent's first frame after connecting to wss://<relay>/agent. */
export interface HelloMsg {
  t: 'hello';
  home_id: string;
  secret: string;
  /** Optional, informational only — agent build/version. */
  agent?: string;
}

/** Relay accepts the agent. */
export interface HelloOkMsg {
  t: 'hello_ok';
  /** Server time (unix ms), handy for the agent to sanity-check clock skew. */
  now: number;
}

// ───────────────────────── REST proxying ───────────────────────────────────

/** relay → agent: forward a buffered client HTTP request. */
export interface ReqMsg {
  t: 'req';
  id: string;
  method: string;
  /** Path AFTER /h/:home_id, including query string. e.g. "/api/v1/status?x=1". */
  path: string;
  /** Flattened request headers (Authorization is forwarded untouched). */
  headers: Record<string, string>;
  /** Request body as base64, or null if empty. */
  body: string | null;
}

/** agent → relay: the response for a prior `req`. */
export interface ResMsg {
  t: 'res';
  id: string;
  status: number;
  headers: Record<string, string>;
  /** Response body as base64, or null if empty. Handles binary (JPEG, etc.). */
  body: string | null;
}

// ───────────────────────── WebSocket bridging ──────────────────────────────

/** relay → agent: a client opened a WS; please open the matching upstream WS. */
export interface WsOpenMsg {
  t: 'ws_open';
  cid: string;
  /** Path AFTER /h/:home_id, WITHOUT query (query is carried separately). */
  path: string;
  /** Raw query string WITHOUT the leading "?", e.g. "token=abc". May be "". */
  query: string;
  /** Flattened upgrade-request headers (forwarded untouched). */
  headers: Record<string, string>;
}

/** agent → relay: upstream WS is open and ready. */
export interface WsOpenOkMsg {
  t: 'ws_open_ok';
  cid: string;
}

/** agent → relay: upstream WS could not be opened; relay closes the client WS. */
export interface WsErrMsg {
  t: 'ws_err';
  cid: string;
  error: string;
}

/** both ways: a single text frame's payload for a channel. */
export interface WsMsgMsg {
  t: 'ws_msg';
  cid: string;
  /** The text frame payload, verbatim. */
  data: string;
}

/** both ways: close a channel. */
export interface WsCloseMsg {
  t: 'ws_close';
  cid: string;
  /** Optional WS close code/reason, best-effort. */
  code?: number;
  reason?: string;
}

// ───────────────────────── Keepalive ───────────────────────────────────────

export interface PingMsg {
  t: 'ping';
}
export interface PongMsg {
  t: 'pong';
}

// ───────────────────────── Unions ──────────────────────────────────────────

/** Anything the relay may SEND to an agent. */
export type RelayToAgent = HelloOkMsg | ReqMsg | WsOpenMsg | WsMsgMsg | WsCloseMsg | PingMsg | PongMsg;

/** Anything the relay may RECEIVE from an agent. */
export type AgentToRelay =
  | HelloMsg
  | ResMsg
  | WsOpenOkMsg
  | WsErrMsg
  | WsMsgMsg
  | WsCloseMsg
  | PingMsg
  | PongMsg;

/** Loose shape used while validating an inbound frame before narrowing. */
export interface RawMsg {
  t?: unknown;
  [k: string]: unknown;
}

/** Parse a WS text frame into a tagged object, or null if it isn't valid JSON with a string `t`. */
export function parseFrame(raw: string): RawMsg | null {
  let obj: unknown;
  try {
    obj = JSON.parse(raw);
  } catch {
    return null;
  }
  if (obj === null || typeof obj !== 'object' || Array.isArray(obj)) return null;
  const msg = obj as RawMsg;
  if (typeof msg.t !== 'string') return null;
  return msg;
}
