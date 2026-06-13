//
// Global UI state (Zustand). Deliberately small — server data is fetched per
// screen via the api client; this holds session/connection state and toasts.
//
import { create } from 'zustand';
import type {
  EdgeDeviceDTO,
  LabStepDTO,
  ReadingDTO,
  ServerStatus,
} from '../api/contract';

export interface Toast {
  id: number;
  level: 'info' | 'good' | 'bad';
  message: string;
}

interface AppState {
  paired: boolean | null; // null = still checking
  status: ServerStatus | null;
  online: boolean; // event socket connected
  remote: boolean; // true when going through the relay

  setPaired: (v: boolean) => void;
  setStatus: (s: ServerStatus | null) => void;
  setOnline: (v: boolean) => void;
  setRemote: (v: boolean) => void;

  toasts: Toast[];
  pushToast: (level: Toast['level'], message: string) => void;
  dismissToast: (id: number) => void;

  // ── Device fabric slices ──────────────────────────────────────────────────

  /** Edge devices keyed by device_id. */
  edgeDevices: Record<string, EdgeDeviceDTO>;
  upsertEdgeDevice: (d: EdgeDeviceDTO) => void;
  setEdgeDeviceOnline: (device_id: string, online: boolean) => void;

  /** Latest instrument readings keyed by instrument_id. */
  instrumentReadings: Record<string, ReadingDTO>;
  upsertReading: (r: ReadingDTO) => void;

  /** Lab-session steps keyed by `${session_id}:${step_no}`. */
  labSteps: Record<string, LabStepDTO & { session_id: number }>;
  upsertLabStep: (
    session_id: number,
    step: Omit<LabStepDTO, never> & { session_id?: number },
  ) => void;
}

let toastSeq = 1;

export const useApp = create<AppState>((set) => ({
  paired: null,
  status: null,
  online: false,
  remote: false,

  setPaired: (v) => set({ paired: v }),
  setStatus: (s) => set({ status: s }),
  setOnline: (v) => set({ online: v }),
  setRemote: (v) => set({ remote: v }),

  toasts: [],
  pushToast: (level, message) =>
    set((s) => {
      const id = toastSeq++;
      // auto-dismiss
      setTimeout(() => {
        set((cur) => ({ toasts: cur.toasts.filter((t) => t.id !== id) }));
      }, 4000);
      return { toasts: [...s.toasts, { id, level, message }] };
    }),
  dismissToast: (id) =>
    set((s) => ({ toasts: s.toasts.filter((t) => t.id !== id) })),

  // ── Device fabric ─────────────────────────────────────────────────────────

  edgeDevices: {},
  upsertEdgeDevice: (d) =>
    set((s) => ({
      edgeDevices: { ...s.edgeDevices, [d.device_id]: d },
    })),
  setEdgeDeviceOnline: (device_id, online) =>
    set((s) => {
      const existing = s.edgeDevices[device_id];
      if (!existing) return {};
      return {
        edgeDevices: {
          ...s.edgeDevices,
          [device_id]: { ...existing, online },
        },
      };
    }),

  instrumentReadings: {},
  upsertReading: (r) =>
    set((s) => ({
      instrumentReadings: { ...s.instrumentReadings, [r.instrument_id]: r },
    })),

  labSteps: {},
  upsertLabStep: (session_id, step) =>
    set((s) => {
      const key = `${session_id}:${step.step_no}`;
      return {
        labSteps: {
          ...s.labSteps,
          [key]: { ...step, session_id } as LabStepDTO & { session_id: number },
        },
      };
    }),
}));
