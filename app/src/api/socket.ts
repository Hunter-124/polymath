//
// Live event stream (server → client) over WebSocket, mirroring the EventBus.
// Auto-reconnects with backoff and re-applies subscriptions. Browsers can't set
// WS headers, so the device token rides as a query param (gateway + relay both
// accept ?token=).
//
import type {
  ClientCommand,
  DevicePresenceEvent,
  EdgeDeviceDTO,
  LabStepEvent,
  ReadingDTO,
  ServerEvent,
  ServerEventType,
} from './contract';
import { getTokenSync } from './auth';
import { resolveWsUrl, invalidateBase } from './transport';
import { useApp } from '../state/store';

type Handler = (e: ServerEvent) => void;
type StateHandler = (connected: boolean) => void;

class EventSocket {
  private ws: WebSocket | null = null;
  private handlers = new Set<Handler>();
  private stateHandlers = new Set<StateHandler>();
  private topics = new Set<ServerEventType>();
  private cameraIds = new Set<number>();
  private backoff = 1000;
  private readonly maxBackoff = 20_000;
  private wantOpen = false;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;

  start(): void {
    this.wantOpen = true;
    void this.open();
  }

  stop(): void {
    this.wantOpen = false;
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.ws?.close();
    this.ws = null;
  }

  on(h: Handler): () => void {
    this.handlers.add(h);
    return () => this.handlers.delete(h);
  }

  onState(h: StateHandler): () => void {
    this.stateHandlers.add(h);
    return () => this.stateHandlers.delete(h);
  }

  subscribe(topics: ServerEventType[], cameraIds: number[] = []): void {
    topics.forEach((t) => this.topics.add(t));
    cameraIds.forEach((c) => this.cameraIds.add(c));
    this.send({ type: 'subscribe', topics, camera_ids: cameraIds });
  }

  unsubscribe(topics: ServerEventType[]): void {
    topics.forEach((t) => this.topics.delete(t));
    this.send({ type: 'unsubscribe', topics });
  }

  private send(cmd: ClientCommand): void {
    if (this.ws?.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(cmd));
    }
  }

  private async open(): Promise<void> {
    if (!this.wantOpen) return;
    let url: string;
    try {
      url = await resolveWsUrl(getTokenSync());
    } catch {
      this.scheduleReconnect();
      return;
    }

    const ws = new WebSocket(url);
    this.ws = ws;

    ws.onopen = () => {
      this.backoff = 1000;
      this.emitState(true);
      // Re-apply any subscriptions made before (re)connect.
      if (this.topics.size) {
        this.send({
          type: 'subscribe',
          topics: [...this.topics],
          camera_ids: [...this.cameraIds],
        });
      }
    };

    ws.onmessage = (ev) => {
      try {
        const data = JSON.parse(ev.data as string) as ServerEvent;
        this.handlers.forEach((h) => h(data));
        // Route new fabric event types directly into the Zustand store so any
        // screen that selects from the store updates live without its own handler.
        this.routeFabricEvent(data);
      } catch {
        /* ignore malformed frame */
      }
    };

    ws.onclose = () => {
      this.emitState(false);
      if (this.ws === ws) this.ws = null;
      this.scheduleReconnect();
    };

    ws.onerror = () => {
      invalidateBase(); // the live path may have changed; re-probe on reconnect
      ws.close();
    };
  }

  private scheduleReconnect(): void {
    if (!this.wantOpen || this.reconnectTimer) return;
    const delay = this.backoff;
    this.backoff = Math.min(this.backoff * 2, this.maxBackoff);
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      void this.open();
    }, delay);
  }

  private emitState(connected: boolean): void {
    this.stateHandlers.forEach((h) => h(connected));
  }

  /** Pipe fabric-specific events into the Zustand store. */
  private routeFabricEvent(e: ServerEvent): void {
    const store = useApp.getState();
    switch (e.type) {
      case 'instrument_reading': {
        const r = e.data as ReadingDTO;
        store.upsertReading(r);
        break;
      }
      case 'device_presence': {
        const p = e.data as DevicePresenceEvent;
        if (store.edgeDevices[p.device_id]) {
          // Device already known — just flip online flag.
          store.setEdgeDeviceOnline(p.device_id, p.online);
        } else {
          // First-seen: create a minimal EdgeDeviceDTO so the Devices screen
          // updates without a full refetch.
          const stub: EdgeDeviceDTO = {
            device_id: p.device_id,
            kind: p.kind,
            name: p.name,
            location: '',
            transport: '',
            endpoint: '',
            capabilities: {},
            fw_version: '',
            last_seen: p.ts,
            enabled: true,
            online: p.online,
          };
          store.upsertEdgeDevice(stub);
        }
        break;
      }
      case 'lab_step': {
        const s = e.data as LabStepEvent;
        // Map LabStepEvent → LabStepDTO shape expected by the store.
        store.upsertLabStep(s.session_id, {
          step_no: s.step_no,
          prompt: s.prompt,
          expected_kind: '',
          expected_unit: s.unit,
          measured_value: s.measured_value,
          measured_unit: s.unit,
          verified: s.verified,
          verified_at: s.verified ? Math.floor(Date.now() / 1000) : null,
        });
        break;
      }
    }
  }
}

// One shared socket for the whole app.
export const socket = new EventSocket();
