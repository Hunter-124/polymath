//
// Edge-device registry (device fabric v2). Lists EdgeDeviceDTOs from
// /fabric/devices. Camera rows link to their clip browser. Presence is kept
// live via `device_presence` WS events piped through the Zustand store.
//
import { useCallback, useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { EmptyState, Loading, relativeTime } from '../components/ui';
import { Icon } from '../components/icons';
import { api } from '../api/client';
import { socket } from '../api/socket';
import { useApp } from '../state/store';
import type { EdgeDeviceDTO } from '../api/contract';

const KIND_LABEL: Record<string, string> = {
  camera: 'Camera',
  voice_sat: 'Voice Satellite',
  instrument: 'Instrument',
  panel: 'Panel',
};

function OnlineDot({ online }: { online: boolean }) {
  return (
    <span
      className={`dot${online ? ' good' : ''}`}
      style={{
        marginTop: 6,
      }}
    />
  );
}

export function FabricDevicesScreen() {
  const [loading, setLoading] = useState(true);
  const pushToast = useApp((s) => s.pushToast);
  const edgeDevices = useApp((s) => s.edgeDevices);
  const upsertEdgeDevice = useApp((s) => s.upsertEdgeDevice);
  const navigate = useNavigate();

  const load = useCallback(() => {
    setLoading(true);
    api
      .fabricDevices()
      .then((rows: EdgeDeviceDTO[]) => {
        rows.forEach(upsertEdgeDevice);
        setLoading(false);
      })
      .catch((e) => {
        setLoading(false);
        pushToast('bad', `Couldn't load edge devices: ${(e as Error).message}`);
      });
  }, [upsertEdgeDevice, pushToast]);

  useEffect(() => {
    load();
  }, [load]);

  // Subscribe to presence events so the store (and this screen) auto-update.
  useEffect(() => {
    socket.subscribe(['device_presence']);
    return () => socket.unsubscribe(['device_presence']);
  }, []);

  const sorted = Object.values(edgeDevices).sort((a, b) =>
    a.name.localeCompare(b.name),
  );

  if (loading && sorted.length === 0) return <Loading />;

  return (
    <div className="app-content">
      <div className="toolbar">
        <button className="btn ghost icon" onClick={load} aria-label="Refresh devices">
          <Icon name="refresh" size={16} />
        </button>
      </div>

      {sorted.length === 0 ? (
        <EmptyState
          icon={<Icon name="fabric" size={34} />}
          title="No edge devices"
          hint="Devices announce themselves automatically when they come online."
        />
      ) : (
        <div className="stack">
        {sorted.map((d) => {
          const openClips = () => navigate(`/clips/${encodeURIComponent(d.device_id)}`);
          return (
          <div
            key={d.device_id}
            className="row"
            role={d.kind === 'camera' ? 'button' : undefined}
            tabIndex={d.kind === 'camera' ? 0 : undefined}
            style={{ alignItems: 'flex-start' }}
            onClick={d.kind === 'camera' ? openClips : undefined}
            onKeyDown={
              d.kind === 'camera'
                ? (e) => (e.key === 'Enter' || e.key === ' ') && openClips()
                : undefined
            }
          >
            <OnlineDot online={d.online} />
            <div style={{ flex: 1 }}>
              <div className="row-title" style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
                {d.name}
                <span className="pill" style={{ fontWeight: 400 }}>
                  {KIND_LABEL[d.kind] ?? d.kind}
                </span>
              </div>
              <div className="row-subtitle">
                {d.location ? `${d.location} · ` : ''}
                {d.fw_version ? `fw ${d.fw_version} · ` : ''}
                seen {relativeTime(d.last_seen)}
              </div>
              {d.endpoint && (
                <div className="faint" style={{ fontSize: 11, marginTop: 2 }}>
                  {d.endpoint}
                </div>
              )}
            </div>
            {d.kind === 'camera' && (
              <span className="faint">
                <Icon name="clip" size={18} />
              </span>
            )}
          </div>
        );
        })}
        </div>
      )}
    </div>
  );
}
