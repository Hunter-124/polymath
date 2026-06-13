//
// Minimal inline icon set (stroke style, 24×24). No icon-font dependency.
//
type IconName =
  | 'chat'
  | 'camera'
  | 'tasks'
  | 'timeline'
  | 'more'
  | 'shopping'
  | 'reminders'
  | 'memory'
  | 'personalities'
  | 'settings'
  | 'devices'
  | 'send'
  | 'mic'
  | 'plus'
  | 'check'
  | 'trash'
  | 'back'
  | 'qr'
  | 'search'
  | 'refresh'
  | 'power'
  | 'fabric'
  | 'instrument'
  | 'lab'
  | 'clip'
  | 'wifi';

const P: Record<IconName, string> = {
  chat: 'M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z',
  camera:
    'M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z M12 17a4 4 0 1 0 0-8 4 4 0 0 0 0 8z',
  tasks: 'M9 11l3 3L22 4 M21 12v7a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11',
  timeline: 'M12 8v4l3 3 M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20z',
  more: 'M4 6h16 M4 12h16 M4 18h16',
  shopping:
    'M6 2L3 6v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2V6l-3-4z M3 6h18 M16 10a4 4 0 0 1-8 0',
  reminders: 'M18 8a6 6 0 0 0-12 0c0 7-3 9-3 9h18s-3-2-3-9 M13.7 21a2 2 0 0 1-3.4 0',
  memory:
    'M9.5 2a4.5 4.5 0 0 0-4.4 5.5A4 4 0 0 0 5 15a4 4 0 0 0 4 4 3 3 0 0 0 3-3V5a3 3 0 0 0-2.5-3z M14.5 2a4.5 4.5 0 0 1 4.4 5.5A4 4 0 0 1 19 15a4 4 0 0 1-4 4 3 3 0 0 1-3-3',
  personalities:
    'M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2 M9 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8z M22 21v-2a4 4 0 0 0-3-3.87 M16 3.13A4 4 0 0 1 16 11',
  settings:
    'M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-2.82 1.17V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 8 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.6 15H4a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 6 8a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 11 4.6V4a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 2.82 1.17l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9H20a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z',
  devices:
    'M5 2h14a1 1 0 0 1 1 1v18a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V3a1 1 0 0 1 1-1z M12 18h.01',
  send: 'M22 2L11 13 M22 2l-7 20-4-9-9-4z',
  mic: 'M12 1a3 3 0 0 0-3 3v8a3 3 0 0 0 6 0V4a3 3 0 0 0-3-3z M19 10a7 7 0 0 1-14 0 M12 19v4',
  plus: 'M12 5v14 M5 12h14',
  check: 'M20 6L9 17l-5-5',
  trash: 'M3 6h18 M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2 M6 6l1 14a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-14',
  back: 'M19 12H5 M12 19l-7-7 7-7',
  qr: 'M3 3h7v7H3z M14 3h7v7h-7z M3 14h7v7H3z M14 14h3v3h-3z M20 14v7 M17 20h4',
  search: 'M11 19a8 8 0 1 0 0-16 8 8 0 0 0 0 16z M21 21l-4.3-4.3',
  refresh: 'M23 4v6h-6 M1 20v-6h6 M3.5 9a9 9 0 0 1 14.8-3.4L23 10 M1 14l4.7 4.4A9 9 0 0 0 20.5 15',
  power: 'M18.4 6.6a9 9 0 1 1-12.8 0 M12 2v10',
  // device fabric icons
  fabric: 'M9 3H5a2 2 0 0 0-2 2v4 M19 3h-4 M21 9V5a2 2 0 0 0-2-2 M3 15v4a2 2 0 0 0 2 2h4 M19 21h2a2 2 0 0 0 2-2v-4 M12 7v10 M7 12h10',
  instrument: 'M9 3v11a3 3 0 0 0 6 0V3 M9 3h6 M6 7h2 M16 7h2',
  lab: 'M9 3h6 M8 3a1 1 0 0 0-1 1v6l-3 9a1 1 0 0 0 .95 1.35h14.1A1 1 0 0 0 20 19l-3-9V4a1 1 0 0 0-1-1 M6 14h12',
  clip: 'M14.5 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7.5L14.5 2z M14 2v6h6 M10 13l3 2-3 2v-4z',
  wifi: 'M5 12.5a9.9 9.9 0 0 1 14 0 M8.5 15.5a5 5 0 0 1 7 0 M12 19h.01',
};

export function Icon({
  name,
  size = 24,
  fill = false,
}: {
  name: IconName;
  size?: number;
  fill?: boolean;
}) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill={fill ? 'currentColor' : 'none'}
      stroke="currentColor"
      strokeWidth={2}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden
    >
      <path d={P[name]} />
    </svg>
  );
}

export type { IconName };
