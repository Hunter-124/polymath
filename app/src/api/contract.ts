//
// Polymath remote API — the shared contract.
//
// This is the single source of truth for the wire format between the mobile/web
// client (app/), the relay (cloud/relay/), and the embedded gateway
// (src/gateway/). The C++ gateway mirrors these shapes in json_map.cpp; the
// human-readable reference lives in docs/API.md.
//
// Types here intentionally mirror src/core/types.h and src/core/schema.h so the
// gateway can serialize straight from the existing structs/tables.
//

export const API_VERSION = 'v1';
export const API_BASE = `/api/${API_VERSION}`;

// Default LAN endpoint (mDNS). The transport layer also accepts a numeric IP.
export const LAN_HOST = 'polymath.local';
export const LAN_PORT = 8765;

// ─── Auth / pairing ────────────────────────────────────────────────────────

export type DeviceRole = 'owner' | 'guest';

/** Encoded in the desktop's pairing QR. */
export interface PairingPayload {
  relay_url: string; // wss://… ; empty string ⇒ LAN-only pairing
  home_id: string; // opaque routing id for this home
  pair_code: string; // short-lived, single-use
  lan_host?: string; // e.g. "polymath.local" or "192.168.1.42"
  lan_port?: number;
}

export interface PairRequest {
  code: string;
  device_name: string;
  pubkey?: string; // base64 X25519 public key for optional E2E (§4 REMOTE_ACCESS)
  platform?: 'ios' | 'android' | 'web';
}

export interface PairResponse {
  token: string; // long-lived device token (Bearer)
  device_id: string;
  role: DeviceRole;
  home_id: string;
  relay_url: string;
  server_pubkey?: string; // base64 X25519 of the gateway, for E2E
  capabilities: ServerCapabilities;
}

export interface ServerCapabilities {
  chat: boolean;
  voice: boolean;
  cameras: boolean;
  vision_find: boolean;
  memory: boolean;
  tasks: boolean;
  reminders: boolean;
  shopping: boolean;
  personalities: boolean;
  e2e: boolean;
  app_version: string;
}

/** A paired device, as shown in the desktop's device manager. */
export interface Device {
  device_id: string;
  name: string;
  role: DeviceRole;
  platform: string;
  last_seen: number; // unix seconds
  created_at: number;
  online: boolean;
}

// ─── Device fabric (v2) — edge cameras / voice / instruments / panels ───────

export type EdgeDeviceKind = 'camera' | 'voice_sat' | 'instrument' | 'panel';

/** A row in the edge-device registry (distinct from a paired phone `Device`). */
export interface EdgeDeviceDTO {
  device_id: string;
  kind: EdgeDeviceKind;
  name: string;
  location: string;
  transport: string; // mqtt|http|mjpeg|rtsp
  endpoint: string; // the device's own HTTP base
  capabilities: Record<string, unknown>;
  fw_version: string;
  last_seen: number;
  enabled: boolean;
  online: boolean;
}

/** Self-registration payload a device POSTs to /fabric/devices/announce. */
export interface DeviceAnnounce {
  device_id: string;
  kind: EdgeDeviceKind;
  name: string;
  location?: string;
  fw?: string;
  endpoint?: string;
  transport?: string;
  capabilities?: Record<string, unknown>;
  instruments?: InstrumentDTO[];
}

export interface InstrumentDTO {
  id: string; // unique_id
  device_id: string;
  name: string;
  channel: number;
  unit: string;
  device_class: string; // mass|temperature|pressure|ph|co2|...
  expected_min: number | null;
  expected_max: number | null;
}

export interface ReadingDTO {
  instrument_id: string;
  value: number;
  unit: string;
  in_range: boolean;
  ts: number;
}

/** Edge-camera detection + clip metadata (POST /cameras/:id/events). */
export interface CameraEventDTO {
  device_id?: string;
  kind: 'motion' | 'person' | 'face';
  confidence?: number;
  thumb_b64?: string;
  clip_url?: string;
  ts?: number;
}

export type LabSessionStatus = 'active' | 'paused' | 'done' | 'canceled';

export interface LabSessionDTO {
  id: number;
  title: string;
  objective: string;
  status: LabSessionStatus;
  report_doc_id: number | null;
  started_at: number;
  ended_at: number | null;
  steps?: LabStepDTO[]; // present on GET /lab/sessions/:id
}

export interface LabStepDTO {
  step_no: number;
  prompt: string;
  expected_kind: string; // temperature|mass|time|ph|...
  expected_unit: string;
  measured_value: number | null;
  measured_unit: string;
  verified: boolean;
  verified_at: number | null;
}

// ─── System / status ───────────────────────────────────────────────────────

export interface ServerStatus {
  listening: boolean; // mirrors AppController::listening
  active_personality: string;
  model_status: string;
  privacy: Record<string, boolean>; // privacy.* toggles
  uptime_s: number;
}

// ─── Chat (mirrors ChatMessage / TokenChunk) ───────────────────────────────

export type ChatRole = 'system' | 'user' | 'assistant' | 'tool';

export interface ChatMessageDTO {
  id?: number;
  role: ChatRole;
  content: string;
  name?: string; // tool name or speaker
  ts: number;
  request_id?: string;
}

export interface ChatSendRequest {
  text: string;
  personality?: string; // optional override for this turn
}

export interface ChatSendResponse {
  request_id: string; // correlate with `token` WS events
}

// ─── Vision (mirrors Frame / Detection / cameras table) ────────────────────

export interface CameraDTO {
  id: number;
  name: string;
  location: string;
  enabled: boolean;
  // stream/snapshot are gateway-proxied so the client never touches the raw
  // camera and remote access keeps working off-LAN.
  snapshot_url: string; // `${API_BASE}/cameras/:id/snapshot`
  stream_url: string; // `${API_BASE}/cameras/:id/stream` (MJPEG or WS frames)
}

export interface BoundingBoxDTO {
  x: number;
  y: number;
  w: number;
  h: number;
  score: number;
  label: string;
}

export interface DetectionDTO {
  camera_id: number;
  boxes: BoundingBoxDTO[];
  user_id?: number;
  ts: number;
}

export interface FindObjectRequest {
  query: string;
}
export interface FindObjectResultDTO {
  query: string;
  answer: string;
  camera_id: number;
  ts: number;
}

// ─── Tasks (mirrors tasks table) ───────────────────────────────────────────

export type TaskStatus = 'queued' | 'running' | 'done' | 'error' | 'canceled';

export interface TaskDTO {
  id: number;
  type: string;
  params: Record<string, unknown>;
  priority: number;
  status: TaskStatus;
  result?: unknown;
  created_at: number;
  updated_at: number;
}

export interface TaskCreateRequest {
  type: string;
  params?: Record<string, unknown>;
  priority?: number;
}

// ─── Reminders (mirrors reminders table) ───────────────────────────────────

export interface ReminderDTO {
  id: number;
  text: string;
  due_at?: number;
  rrule?: string;
  condition?: string;
  fired: boolean;
  created_at: number;
}

export interface ReminderCreateRequest {
  text: string;
  due_at?: number;
  rrule?: string;
  condition?: string;
}

// ─── Shopping (mirrors shopping_items table) ───────────────────────────────

export interface ShoppingItemDTO {
  id: number;
  item: string;
  quantity: string;
  done: boolean;
  created_at: number;
}

export interface ShoppingCreateRequest {
  item: string;
  quantity?: string;
}

// ─── Timeline / events (mirrors events table) ──────────────────────────────

export type EventKind = 'motion' | 'person' | 'face' | 'sound';

export interface TimelineEventDTO {
  id: number;
  kind: EventKind;
  camera_id?: number;
  user_id?: number;
  label: string;
  thumb_url?: string; // gateway-proxied thumbnail
  clip_url?: string; // edge-recorded clip, served from the camera's own SD
  confidence?: number; // on-device detector confidence (edge events)
  ts: number;
}

// ─── Memory (mirrors memories table) ───────────────────────────────────────

export interface MemoryDTO {
  id: number;
  kind: 'note' | 'fact' | 'summary' | 'caption';
  text: string;
  source: string;
  user_id?: number;
  ts: number;
  score?: number; // similarity, on search results
}

export interface MemoryCreateRequest {
  text: string;
  kind?: MemoryDTO['kind'];
}

// ─── Personalities & models ────────────────────────────────────────────────

export interface PersonalityDTO {
  name: string;
  voice: string;
  wake_phrase: string;
  active: boolean;
}

export interface ModelDTO {
  id: string;
  display_name: string;
  role: 'fast' | 'heavy' | 'vision' | 'embedding';
  path: string;
  n_ctx: number;
  n_gpu_layers: number;
  active: boolean;
}

// ─── Settings ──────────────────────────────────────────────────────────────

export interface SettingsPatch {
  key: string;
  value: string; // bool toggles use "true"/"false"
}

// ─── WebSocket envelope (server → client push) ─────────────────────────────
//
// Mirrors the EventBus signal catalog in src/core/event_bus.h. One envelope
// shape; `type` discriminates the payload.

export type ServerEventType =
  | 'token' // TokenChunk            (assistant streaming)
  | 'notice' // Notice               (toast/log)
  | 'utterance' // Utterance          (ASR result)
  | 'detection' // Detection
  | 'frame' // Frame (thumbnail b64)  — only for subscribed cameras
  | 'find_object' // FindObjectResult
  | 'task' // TaskEvent
  | 'reminder' // ReminderFired
  | 'privacy' // PrivacyChanged
  | 'status' // ServerStatus delta
  | 'speak' // SpeakRequest (so the app can play TTS)
  | 'instrument_reading' // ReadingDTO (lab instrument value)
  | 'device_presence' // EdgeDevice online/offline
  | 'lab_step'; // LabStepDTO progress in a guided lab session

export interface ServerEvent<T = unknown> {
  type: ServerEventType;
  ts: number;
  data: T;
}

export interface TokenEvent {
  request_id: string;
  text: string;
  done: boolean;
}
export interface NoticeEvent {
  level: string;
  source: string;
  message: string;
}
export interface SpeakEvent {
  text: string;
  voice: string;
  request_id: string;
  target?: string; // "" => local speaker; else a voice-satellite id to route to
  audio_url?: string; // gateway-rendered TTS, if available
}

export interface DevicePresenceEvent {
  device_id: string;
  kind: EdgeDeviceKind;
  name: string;
  online: boolean;
  ts: number;
}

export interface LabStepEvent {
  session_id: number;
  step_no: number;
  prompt: string;
  status: string; // ask|verifying|verified|out_of_range|done
  measured_value: number;
  unit: string;
  verified: boolean;
}

// Client → server WS control messages (subscriptions).
export type ClientCommandType = 'subscribe' | 'unsubscribe' | 'ping';
export interface ClientCommand {
  type: ClientCommandType;
  topics?: ServerEventType[];
  camera_ids?: number[];
}

// ─── Endpoint map (kept in one place so client & gateway agree) ────────────

export const ENDPOINTS = {
  pair: `${API_BASE}/pair`,
  me: `${API_BASE}/me`,
  status: `${API_BASE}/status`,
  events: `${API_BASE}/events`, // WebSocket

  chat: `${API_BASE}/chat`,
  chatHistory: `${API_BASE}/chat/history`,
  voice: `${API_BASE}/voice`,

  cameras: `${API_BASE}/cameras`,
  cameraSnapshot: (id: number) => `${API_BASE}/cameras/${id}/snapshot`,
  cameraStream: (id: number) => `${API_BASE}/cameras/${id}/stream`,
  cameraEvents: (id: number) => `${API_BASE}/cameras/${id}/events`, // edge ingest (POST)
  cameraFrame: (id: number) => `${API_BASE}/cameras/${id}/frame`, // live tile (POST)
  findObject: `${API_BASE}/find-object`,

  // device fabric (v2)
  fabricDevices: `${API_BASE}/fabric/devices`,
  fabricDevice: (id: string) => `${API_BASE}/fabric/devices/${encodeURIComponent(id)}`,
  fabricAnnounce: `${API_BASE}/fabric/devices/announce`,
  instruments: `${API_BASE}/instruments`,
  instrumentRead: (id: string) => `${API_BASE}/instruments/${encodeURIComponent(id)}/read`,
  labSessions: `${API_BASE}/lab/sessions`,
  labSession: (id: number) => `${API_BASE}/lab/sessions/${id}`,

  tasks: `${API_BASE}/tasks`,
  task: (id: number) => `${API_BASE}/tasks/${id}`,

  reminders: `${API_BASE}/reminders`,
  reminder: (id: number) => `${API_BASE}/reminders/${id}`,

  shopping: `${API_BASE}/shopping`,
  shoppingItem: (id: number) => `${API_BASE}/shopping/${id}`,

  timeline: `${API_BASE}/timeline`,
  memory: `${API_BASE}/memory`,

  personalities: `${API_BASE}/personalities`,
  activePersonality: `${API_BASE}/personalities/active`,
  models: `${API_BASE}/models`,

  settings: `${API_BASE}/settings`,
  setting: (key: string) => `${API_BASE}/settings/${encodeURIComponent(key)}`,

  devices: `${API_BASE}/devices`,
  device: (id: string) => `${API_BASE}/devices/${id}`,
} as const;
