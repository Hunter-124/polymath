//
// Cameras: a live grid of gateway-proxied snapshots (polled, so it works the
// same on-LAN and over the relay), a "find an object" question box backed by the
// vision model, and a motion badge driven by `detection` events. Honors the
// privacy.cameras_enabled master switch.
//
import { useEffect, useRef, useState } from 'react';
import { EmptyState, Loading } from '../components/ui';
import { Icon } from '../components/icons';
import { api, mediaUrl, ApiError } from '../api/client';
import { socket } from '../api/socket';
import type {
  CameraDTO,
  DetectionDTO,
  FindObjectResultDTO,
} from '../api/contract';
import { useApp } from '../state/store';

const SNAPSHOT_INTERVAL = 2000; // ms — poll cadence for the proxied snapshots
const MOTION_TTL = 6000; // ms — how long a motion badge stays lit after an event

// A single camera tile. Resolves the authed snapshot base once, then refreshes
// the <img> by bumping a cache-busting `t` param on an interval.
function CameraTile({
  cam,
  expanded,
  motion,
  onToggle,
}: {
  cam: CameraDTO;
  expanded: boolean;
  motion: boolean;
  onToggle: () => void;
}) {
  const [base, setBase] = useState<string | null>(null);
  const [src, setSrc] = useState<string | null>(null);
  const [failed, setFailed] = useState(false);

  // Resolve the (token-bearing) snapshot URL once per camera.
  useEffect(() => {
    let alive = true;
    void mediaUrl(cam.snapshot_url).then((u) => {
      if (alive) setBase(u);
    });
    return () => {
      alive = false;
    };
  }, [cam.snapshot_url]);

  // Poll: append a timestamp so each fetch is a fresh frame, not the cache.
  useEffect(() => {
    if (!base) return;
    const tick = () => {
      const sep = base.includes('?') ? '&' : '?';
      setSrc(`${base}${sep}t=${Date.now()}`);
    };
    tick();
    const id = setInterval(tick, SNAPSHOT_INTERVAL);
    return () => clearInterval(id);
  }, [base]);

  return (
    <div
      role="button"
      onClick={onToggle}
      style={{
        gridColumn: expanded ? '1 / -1' : 'auto',
        borderRadius: 12,
        overflow: 'hidden',
        position: 'relative',
        background: '#000',
        border: '1px solid var(--line)',
        cursor: 'pointer',
        aspectRatio: '16 / 10',
      }}
    >
      {src && !failed ? (
        <img
          src={src}
          alt={cam.name}
          onError={() => setFailed(true)}
          style={{ width: '100%', height: '100%', objectFit: 'cover', display: 'block' }}
        />
      ) : (
        <div
          style={{
            position: 'absolute',
            inset: 0,
            display: 'grid',
            placeItems: 'center',
            color: 'var(--text-faint)',
          }}
        >
          {failed ? <Icon name="camera" size={28} /> : <div className="spinner" />}
        </div>
      )}

      {motion && (
        <span
          className="pill warn"
          style={{
            position: 'absolute',
            top: 8,
            right: 8,
            background: 'rgba(0,0,0,.55)',
          }}
        >
          ● Motion
        </span>
      )}

      <div
        style={{
          position: 'absolute',
          left: 0,
          right: 0,
          bottom: 0,
          padding: '14px 10px 8px',
          background: 'linear-gradient(transparent, rgba(0,0,0,.7))',
          display: 'flex',
          alignItems: 'baseline',
          gap: 8,
        }}
      >
        <span style={{ color: '#fff', fontWeight: 600, fontSize: 14 }}>{cam.name}</span>
        {cam.location && (
          <span style={{ color: 'rgba(255,255,255,.7)', fontSize: 12 }}>
            {cam.location}
          </span>
        )}
      </div>
    </div>
  );
}

export function CamerasScreen() {
  const [cameras, setCameras] = useState<CameraDTO[] | null>(null);
  const [expandedId, setExpandedId] = useState<number | null>(null);
  const [query, setQuery] = useState('');
  const [finding, setFinding] = useState(false);
  const [result, setResult] = useState<FindObjectResultDTO | null>(null);
  const [motion, setMotion] = useState<Record<number, number>>({}); // camera_id → expiry ts
  const [, forceTick] = useState(0); // re-render so motion badges can expire
  const pushToast = useApp((s) => s.pushToast);
  const status = useApp((s) => s.status);
  const camerasEnabled = status?.privacy?.['privacy.cameras_enabled'] !== false;

  // Keep a live handle to whether cameras are enabled for the effect below.
  const enabledRef = useRef(camerasEnabled);
  enabledRef.current = camerasEnabled;

  // Load the camera list once cameras are permitted.
  useEffect(() => {
    if (!camerasEnabled) {
      setCameras([]);
      return;
    }
    let alive = true;
    api
      .cameras()
      .then((rows) => {
        if (alive) setCameras(rows);
      })
      .catch((e: ApiError) => {
        if (!alive) return;
        setCameras([]);
        pushToast('bad', `Couldn't load cameras: ${e.message}`);
      });
    return () => {
      alive = false;
    };
  }, [camerasEnabled, pushToast]);

  // Live vision events: find-object answers and motion badges.
  useEffect(() => {
    socket.subscribe(['find_object', 'detection']);
    const off = socket.on((e) => {
      if (e.type === 'find_object') {
        setResult(e.data as FindObjectResultDTO);
        setFinding(false);
      } else if (e.type === 'detection') {
        const d = e.data as DetectionDTO;
        if (typeof d.camera_id === 'number') {
          setMotion((m) => ({ ...m, [d.camera_id]: Date.now() + MOTION_TTL }));
        }
      }
    });
    return () => {
      off();
      socket.unsubscribe(['find_object', 'detection']);
    };
  }, []);

  // Expire motion badges roughly every second.
  useEffect(() => {
    const id = setInterval(() => forceTick((n) => n + 1), 1000);
    return () => clearInterval(id);
  }, []);

  async function find() {
    const q = query.trim();
    if (!q || finding) return;
    setFinding(true);
    setResult(null);
    try {
      // The answer may come back on the REST call and/or as a find_object event.
      const res = await api.findObject(q);
      setResult(res);
    } catch (e) {
      pushToast('bad', `Find failed: ${(e as Error).message}`);
    } finally {
      setFinding(false);
    }
  }

  const now = Date.now();

  if (!camerasEnabled) {
    return (
      <div className="app-content">
        <EmptyState
          icon={<Icon name="camera" size={34} />}
          title="Cameras are turned off"
          hint="Enable cameras in Settings ▸ Privacy to view live snapshots and search what they see."
        />
      </div>
    );
  }

  return (
    <div className="app-content">
      <div className="card" style={{ marginBottom: 16 }}>
        <div className="section-label" style={{ margin: '0 0 8px' }}>
          Find an object
        </div>
        <div style={{ display: 'flex', gap: 8 }}>
          <div style={{ position: 'relative', flex: 1 }}>
            <span
              style={{
                position: 'absolute',
                left: 12,
                top: '50%',
                transform: 'translateY(-50%)',
                color: 'var(--text-faint)',
                pointerEvents: 'none',
              }}
            >
              <Icon name="search" size={18} />
            </span>
            <input
              className="input"
              style={{ paddingLeft: 38 }}
              value={query}
              onChange={(e) => setQuery(e.target.value)}
              onKeyDown={(e) => e.key === 'Enter' && find()}
              placeholder="Did I leave my keys on the counter?"
            />
          </div>
        </div>
        {(finding || result) && (
          <div
            className="muted"
            style={{
              marginTop: 10,
              display: 'flex',
              gap: 8,
              alignItems: 'flex-start',
            }}
          >
            {finding ? (
              <>
                <div className="spinner" />
                <span>Looking…</span>
              </>
            ) : result ? (
              <span>{result.answer}</span>
            ) : null}
          </div>
        )}
      </div>

      {cameras === null ? (
        <Loading />
      ) : cameras.length === 0 ? (
        <EmptyState
          icon={<Icon name="camera" size={34} />}
          title="No cameras yet"
          hint="Add cameras on the desktop and they'll show up here."
        />
      ) : (
        <div
          style={{
            display: 'grid',
            gridTemplateColumns: 'repeat(2, 1fr)',
            gap: 10,
          }}
        >
          {cameras.map((cam) => (
            <CameraTile
              key={cam.id}
              cam={cam}
              expanded={expandedId === cam.id}
              motion={(motion[cam.id] ?? 0) > now}
              onToggle={() =>
                setExpandedId((cur) => (cur === cam.id ? null : cam.id))
              }
            />
          ))}
        </div>
      )}
    </div>
  );
}
