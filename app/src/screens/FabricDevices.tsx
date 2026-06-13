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
      style={{
        width: 9,
        height: 9,
        borderRadius: '50%',
        flex: '0 0 auto',
        marginTop: 6,
        background: online ? 'var(--good)' : 'var(--text-faint)',
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
      <div
        style={{
          display: 'flex',
          justifyContent: 'flex-end',
          marginBottom: 10,
        }}
      >
        <button className="btn ghost" style={{ padding: '0 12px', minHeight: 36 }} onClick={load}>
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
        sorted.map((d) => (
          <div
            key={d.device_id}
            className="row"
            role={d.kind === 'camera' ? 'button' : undefined}
            style={{ alignItems: 'flex-start', cursor: d.kind === 'camera' ? 'pointer' : 'default' }}
            onClick={
              d.kind === 'camera'
                ? () => navigate(`/clips/${encodeURIComponent(d.device_id)}`)
                : undefined
            }
          >
            <OnlineDot online={d.online} />
            <div style={{ flex: 1 }}>
              <div style={{ fontWeight: 600, display: 'flex', gap: 6, alignItems: 'center' }}>
                {d.name}
                <span className="pill" style={{ fontWeight: 400 }}>
                  {KIND_LABEL[d.kind] ?? d.kind}
                </span>
              </div>
              <div className="faint">
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
        ))
      )}
    </div>
  );
}
