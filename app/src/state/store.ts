//
// Global UI state (Zustand). Deliberately small — server data is fetched per
// screen via the api client; this holds session/connection state and toasts.
//
import { create } from 'zustand';
import type { ServerStatus } from '../api/contract';

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
}));
