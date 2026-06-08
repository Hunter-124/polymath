//
// Paired devices: who can reach this home, last seen, and a revoke button.
//
import { useCallback, useEffect, useState } from 'react';
import { Button, EmptyState, Loading, relativeTime } from '../components/ui';
import { Icon } from '../components/icons';
import { api } from '../api/client';
import type { Device } from '../api/contract';
import { useApp } from '../state/store';

export function DevicesScreen() {
  const [devices, setDevices] = useState<Device[] | null>(null);
  const pushToast = useApp((s) => s.pushToast);

  const load = useCallback(() => {
    api
      .devices()
      .then(setDevices)
      .catch((e) => {
        setDevices([]);
        pushToast('bad', `Couldn't load devices: ${(e as Error).message}`);
      });
  }, [pushToast]);

  useEffect(() => {
    load();
  }, [load]);

  async function revoke(d: Device) {
    if (!confirm(`Revoke “${d.name}”? It will be signed out immediately.`)) return;
    try {
      await api.revokeDevice(d.device_id);
      setDevices((prev) => prev?.filter((x) => x.device_id !== d.device_id) ?? prev);
    } catch (e) {
      pushToast('bad', `Couldn't revoke: ${(e as Error).message}`);
    }
  }

  return (
    <div className="app-content">
      {devices === null ? (
        <Loading />
      ) : devices.length === 0 ? (
        <EmptyState icon={<Icon name="devices" size={34} />} title="No devices paired" />
      ) : (
        devices.map((d) => (
          <div className="row" key={d.device_id} style={{ alignItems: 'flex-start' }}>
            <span
              style={{
                width: 9,
                height: 9,
                borderRadius: '50%',
                marginTop: 7,
                flex: '0 0 auto',
                background: d.online ? 'var(--good)' : 'var(--text-faint)',
              }}
            />
            <div style={{ flex: 1 }}>
              <div style={{ fontWeight: 600 }}>
                {d.name}
                {d.role === 'owner' && (
                  <span className="pill" style={{ marginLeft: 6 }}>
                    owner
                  </span>
                )}
              </div>
              <div className="faint">
                {d.platform} · seen {relativeTime(d.last_seen)}
              </div>
            </div>
            <Button
              variant="danger"
              onClick={() => revoke(d)}
              style={{ minHeight: 36, padding: '0 12px' }}
            >
              Revoke
            </Button>
          </div>
        ))
      )}
    </div>
  );
}
