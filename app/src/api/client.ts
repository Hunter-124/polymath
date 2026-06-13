//
// Typed REST client. Every method resolves the live base URL (LAN or relay),
// attaches the device token, and maps a 401 to an automatic un-pair so the UI
// drops back to the pairing screen.
//
import { ENDPOINTS } from './contract';
import type {
  CameraDTO,
  ChatMessageDTO,
  ChatSendRequest,
  ChatSendResponse,
  Device,
  EdgeDeviceDTO,
  FindObjectResultDTO,
  InstrumentDTO,
  LabSessionDTO,
  MemoryDTO,
  ModelDTO,
  PersonalityDTO,
  ReadingDTO,
  ReminderCreateRequest,
  ReminderDTO,
  ServerStatus,
  SettingsPatch,
  ShoppingCreateRequest,
  ShoppingItemDTO,
  TaskCreateRequest,
  TaskDTO,
  TimelineEventDTO,
} from './contract';
import { loadToken, getTokenSync, unpair } from './auth';
import { resolveHttpBase, invalidateBase } from './transport';

export class ApiError extends Error {
  constructor(
    message: string,
    public status: number,
  ) {
    super(message);
  }
}

async function req<T>(path: string, init: RequestInit = {}): Promise<T> {
  const base = await resolveHttpBase();
  const tok = await loadToken();
  const headers = new Headers(init.headers);
  if (tok) headers.set('Authorization', `Bearer ${tok}`);
  if (init.body && !headers.has('Content-Type')) {
    headers.set('Content-Type', 'application/json');
  }

  let r: Response;
  try {
    r = await fetch(`${base}${path}`, { ...init, headers });
  } catch (e) {
    invalidateBase(); // network path may have changed; re-probe next call
    throw new ApiError((e as Error).message || 'network_error', 0);
  }

  if (r.status === 401) {
    await unpair();
    throw new ApiError('unauthorized', 401);
  }
  if (!r.ok) {
    throw new ApiError(await r.text().catch(() => r.statusText), r.status);
  }
  if (r.status === 204) return undefined as T;
  const ct = r.headers.get('content-type') ?? '';
  return (ct.includes('application/json') ? await r.json() : await r.text()) as T;
}

const json = (body: unknown): RequestInit => ({
  method: 'POST',
  body: JSON.stringify(body),
});

/** Absolute URL for an <img>/<video> GET that can't carry an auth header. */
export async function mediaUrl(path: string): Promise<string> {
  const base = await resolveHttpBase();
  const tok = getTokenSync();
  const sep = path.includes('?') ? '&' : '?';
  return `${base}${path}${tok ? `${sep}token=${encodeURIComponent(tok)}` : ''}`;
}

export const api = {
  // system
  status: () => req<ServerStatus>(ENDPOINTS.status),

  // chat
  sendChat: (body: ChatSendRequest) =>
    req<ChatSendResponse>(ENDPOINTS.chat, json(body)),
  chatHistory: (limit = 50) =>
    req<ChatMessageDTO[]>(`${ENDPOINTS.chatHistory}?limit=${limit}`),

  // cameras / vision
  cameras: () => req<CameraDTO[]>(ENDPOINTS.cameras),
  findObject: (query: string) =>
    req<FindObjectResultDTO>(ENDPOINTS.findObject, json({ query })),

  // tasks
  tasks: () => req<TaskDTO[]>(ENDPOINTS.tasks),
  createTask: (body: TaskCreateRequest) =>
    req<TaskDTO>(ENDPOINTS.tasks, json(body)),
  updateTask: (id: number, patch: Partial<Pick<TaskDTO, 'status' | 'priority'>>) =>
    req<TaskDTO>(ENDPOINTS.task(id), { method: 'PATCH', body: JSON.stringify(patch) }),

  // reminders
  reminders: () => req<ReminderDTO[]>(ENDPOINTS.reminders),
  createReminder: (body: ReminderCreateRequest) =>
    req<ReminderDTO>(ENDPOINTS.reminders, json(body)),
  deleteReminder: (id: number) =>
    req<void>(ENDPOINTS.reminder(id), { method: 'DELETE' }),

  // shopping
  shopping: () => req<ShoppingItemDTO[]>(ENDPOINTS.shopping),
  addShopping: (body: ShoppingCreateRequest) =>
    req<ShoppingItemDTO>(ENDPOINTS.shopping, json(body)),
  toggleShopping: (id: number, done: boolean) =>
    req<ShoppingItemDTO>(ENDPOINTS.shoppingItem(id), {
      method: 'PATCH',
      body: JSON.stringify({ done }),
    }),
  deleteShopping: (id: number) =>
    req<void>(ENDPOINTS.shoppingItem(id), { method: 'DELETE' }),

  // timeline / memory
  timeline: (since?: number) =>
    req<TimelineEventDTO[]>(
      `${ENDPOINTS.timeline}${since ? `?since=${since}` : ''}`,
    ),
  searchMemory: (q: string) =>
    req<MemoryDTO[]>(`${ENDPOINTS.memory}?q=${encodeURIComponent(q)}`),
  addMemory: (text: string) => req<MemoryDTO>(ENDPOINTS.memory, json({ text })),

  // personalities / models
  personalities: () => req<PersonalityDTO[]>(ENDPOINTS.personalities),
  setPersonality: (name: string) =>
    req<void>(ENDPOINTS.activePersonality, json({ name })),
  models: () => req<ModelDTO[]>(ENDPOINTS.models),

  // settings
  settings: () => req<Record<string, string>>(ENDPOINTS.settings),
  patchSetting: (body: SettingsPatch) =>
    req<void>(ENDPOINTS.setting(body.key), {
      method: 'PATCH',
      body: JSON.stringify({ value: body.value }),
    }),

  // devices (owner only)
  devices: () => req<Device[]>(ENDPOINTS.devices),
  revokeDevice: (id: string) =>
    req<void>(ENDPOINTS.device(id), { method: 'DELETE' }),

  // device fabric (v2)
  fabricDevices: () => req<EdgeDeviceDTO[]>(ENDPOINTS.fabricDevices),
  instruments: () => req<InstrumentDTO[]>(ENDPOINTS.instruments),
  instrumentRead: (id: string) =>
    req<ReadingDTO>(ENDPOINTS.instrumentRead(id)),

  // lab sessions
  labSessions: () => req<LabSessionDTO[]>(ENDPOINTS.labSessions),
  labSession: (id: number) => req<LabSessionDTO>(ENDPOINTS.labSession(id)),
  createLabSession: (body: { title: string; objective: string }) =>
    req<LabSessionDTO>(ENDPOINTS.labSessions, json(body)),
  updateLabSession: (
    id: number,
    patch: Partial<Pick<LabSessionDTO, 'status'>>,
  ) =>
    req<LabSessionDTO>(ENDPOINTS.labSession(id), {
      method: 'PATCH',
      body: JSON.stringify(patch),
    }),
};
