//
// Timeline of what the home noticed (motion / person / face / sound), with
// filter chips and live prepend on `detection` events.
//
import { useEffect, useState } from 'react';
import { EmptyState, Loading, relativeTime } from '../components/ui';
import { Icon, type IconName } from '../components/icons';
import { api, mediaUrl } from '../api/client';
import { socket } from '../api/socket';
import type { DetectionDTO, EventKind, TimelineEventDTO } from '../api/contract';
import { useApp } from '../state/store';

const KIND_ICON: Record<EventKind, IconName> = {
  motion: 'camera',
  person: 'personalities',
  face: 'personalities',
  sound: 'reminders',
};
const FILTERS: (EventKind | 'all')[] = ['all', 'motion', 'person', 'face', 'sound'];

function Thumb({ url }: { url: string }) {
  const [src, setSrc] = useState<string | null>(null);
  useEffect(() => {
    let alive = true;
    void mediaUrl(url).then((u) => alive && setSrc(u));
    return () => {
      alive = false;
    };
  }, [url]);
  if (!src) return null;
  return (
    <img
      src={src}
      alt=""
      style={{ width: 54, height: 54, borderRadius: 8, objectFit: 'cover', flex: '0 0 auto' }}
    />
  );
}

export function TimelineScreen() {
  const [events, setEvents] = useState<TimelineEventDTO[] | null>(null);
  const [filter, setFilter] = useState<EventKind | 'all'>('all');
  const pushToast = useApp((s) => s.pushToast);

  useEffect(() => {
    api
      .timeline()
      .then(setEvents)
      .catch((e) => {
        setEvents([]);
        pushToast('bad', `Couldn't load timeline: ${(e as Error).message}`);
      });
  }, [pushToast]);

  useEffect(() => {
    socket.subscribe(['detection']);
    const off = socket.on((e) => {
      if (e.type !== 'detection') return;
      const d = e.data as DetectionDTO;
      const item: TimelineEventDTO = {
        id: -Date.now(),
        kind: 'motion',
        camera_id: d.camera_id,
        label: d.boxes?.[0]?.label ?? 'motion',
        ts: d.ts ?? Math.floor(Date.now() / 1000),
      };
      setEvents((prev) => (prev ? [item, ...prev] : [item]));
    });
    return () => {
      off();
      socket.unsubscribe(['detection']);
    };
  }, []);

  const shown =
    events?.filter((e) => filter === 'all' || e.kind === filter) ?? null;

  return (
    <div className="app-content">
      <div style={{ display: 'flex', gap: 8, overflowX: 'auto', marginBottom: 14 }}>
        {FILTERS.map((f) => (
          <button
            key={f}
            className={`pill${filter === f ? ' good' : ''}`}
            style={{ border: 'none', cursor: 'pointer', textTransform: 'capitalize' }}
            onClick={() => setFilter(f)}
          >
            {f}
          </button>
        ))}
      </div>

      {shown === null ? (
        <Loading />
      ) : shown.length === 0 ? (
        <EmptyState
          icon={<Icon name="timeline" size={34} />}
          title="Nothing here yet"
          hint="Motion, people, and sounds the assistant notices will appear here."
        />
      ) : (
        shown.map((ev) => (
          <div className="row" key={ev.id}>
            {ev.thumb_url ? (
              <Thumb url={ev.thumb_url} />
            ) : (
              <span style={{ color: 'var(--text-faint)', flex: '0 0 auto' }}>
                <Icon name={KIND_ICON[ev.kind]} size={22} />
              </span>
            )}
            <div style={{ flex: 1 }}>
              <div style={{ fontWeight: 600, textTransform: 'capitalize' }}>
                {ev.label || ev.kind}
              </div>
              <div className="faint">
                {ev.kind}
                {ev.camera_id != null ? ` · cam ${ev.camera_id}` : ''}
              </div>
            </div>
            <span className="faint">{relativeTime(ev.ts)}</span>
          </div>
        ))
      )}
    </div>
  );
}
