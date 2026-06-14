//
// Clip browser for a direct-paired edge camera. Uses deviceDirect to list and
// play clips straight from the camera's SD card. Falls back to showing a
// gateway timeline clip_url when direct pairing is unavailable.
//
// Route: /clips/:deviceId
//
import { useCallback, useEffect, useState } from 'react';
import { useParams } from 'react-router-dom';
import { EmptyState, Loading } from '../components/ui';
import { Icon } from '../components/icons';
import { useApp } from '../state/store';
import {
  loadPairedCamera,
  makeDeviceClient,
  type ClipInfo,
  type PairedCamera,
} from '../api/deviceDirect';
import { api } from '../api/client';
import type { TimelineEventDTO } from '../api/contract';

// ─── Direct (device) clip browser ─────────────────────────────────────────

function DirectClips({ cam }: { cam: PairedCamera }) {
  const [clips, setClips] = useState<ClipInfo[] | null>(null);
  const [playUrl, setPlayUrl] = useState<string | null>(null);
  const pushToast = useApp((s) => s.pushToast);
  const client = makeDeviceClient(cam);

  const load = useCallback(() => {
    client
      .clips()
      .then(setClips)
      .catch((e) => {
        setClips([]);
        pushToast('bad', `Couldn't list clips: ${(e as Error).message}`);
      });
  }, [pushToast]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    load();
  }, [load]);

  async function play(clip: ClipInfo) {
    try {
      const url = await client.clipUrl(clip.file);
      setPlayUrl(url);
    } catch (e) {
      pushToast('bad', `Couldn't get clip URL: ${(e as Error).message}`);
    }
  }

  if (clips === null) return <Loading />;

  return (
    <>
      {playUrl && (
        <div style={{ marginBottom: 12 }}>
          <video
            src={playUrl}
            controls
            autoPlay
            style={{ width: '100%', borderRadius: 10, background: '#000' }}
          />
          <button
            className="btn ghost"
            style={{ marginTop: 6, width: '100%' }}
            onClick={() => setPlayUrl(null)}
          >
            Close
          </button>
        </div>
      )}

      {clips.length === 0 ? (
        <EmptyState
          icon={<Icon name="clip" size={34} />}
          title="No clips yet"
          hint="The camera saves clips to its SD card when it detects motion."
        />
      ) : (
        clips.map((c) => (
          <div
            key={c.file}
            className="row"
            role="button"
            style={{ cursor: 'pointer' }}
            onClick={() => play(c)}
          >
            <span style={{ color: 'var(--accent)', flex: '0 0 auto' }}>
              <Icon name="clip" size={20} />
            </span>
            <div style={{ flex: 1 }}>
              <div style={{ fontWeight: 500 }}>{c.file}</div>
              {c.ts && (
                <div className="faint">
                  {new Date(c.ts * 1000).toLocaleString()}
                </div>
              )}
              {c.size && (
                <div className="faint">{(c.size / 1024).toFixed(0)} KB</div>
              )}
            </div>
            <span className="faint">▶</span>
          </div>
        ))
      )}
    </>
  );
}

// ─── Fallback: gateway timeline clips ─────────────────────────────────────

function GatewayClips({ deviceId }: { deviceId: string }) {
  const [events, setEvents] = useState<TimelineEventDTO[] | null>(null);
  const pushToast = useApp((s) => s.pushToast);
  const [playUrl, setPlayUrl] = useState<string | null>(null);

  useEffect(() => {
    // Load all timeline events and filter for this device's clips.
    api
      .timeline()
      .then((rows) => {
        setEvents(rows.filter((e) => e.clip_url));
      })
      .catch((e) => {
        setEvents([]);
        pushToast('bad', `Couldn't load clips: ${(e as Error).message}`);
      });
  }, [pushToast, deviceId]);

  if (events === null) return <Loading />;

  return (
    <>
      <div
        className="faint"
        style={{
          display: 'flex',
          gap: 6,
          alignItems: 'center',
          marginBottom: 10,
          fontSize: 13,
        }}
      >
        <Icon name="wifi" size={14} />
        Showing gateway-cached clip URLs — scan the camera QR to browse clips
        directly.
      </div>

      {playUrl && (
        <div style={{ marginBottom: 12 }}>
          <video
            src={playUrl}
            controls
            autoPlay
            style={{ width: '100%', borderRadius: 10, background: '#000' }}
          />
          <button
            className="btn ghost"
            style={{ marginTop: 6, width: '100%' }}
            onClick={() => setPlayUrl(null)}
          >
            Close
          </button>
        </div>
      )}

      {events.length === 0 ? (
        <EmptyState
          icon={<Icon name="clip" size={34} />}
          title="No clips"
          hint="Clips from detected events will appear here."
        />
      ) : (
        events.map((ev) => (
          <div
            key={ev.id}
            className="row"
            role="button"
            style={{ cursor: ev.clip_url ? 'pointer' : 'default' }}
            onClick={() => ev.clip_url && setPlayUrl(ev.clip_url)}
          >
            <span style={{ color: 'var(--accent)', flex: '0 0 auto' }}>
              <Icon name="clip" size={20} />
            </span>
            <div style={{ flex: 1 }}>
              <div style={{ fontWeight: 500, textTransform: 'capitalize' }}>
                {ev.label || ev.kind}
              </div>
              <div className="faint">
                {new Date(ev.ts * 1000).toLocaleString()}
              </div>
            </div>
            {ev.clip_url && <span className="faint">▶</span>}
          </div>
        ))
      )}
    </>
  );
}

// ─── Main screen ──────────────────────────────────────────────────────────

export function CameraClipsScreen() {
  const { deviceId } = useParams<{ deviceId: string }>();
  const [pairedCam, setPairedCam] = useState<PairedCamera | null | undefined>(
    undefined, // undefined = loading
  );
  const edgeDevices = useApp((s) => s.edgeDevices);
  const deviceName =
    deviceId && edgeDevices[deviceId]
      ? edgeDevices[deviceId].name
      : deviceId ?? 'Camera';

  useEffect(() => {
    if (!deviceId) {
      setPairedCam(null);
      return;
    }
    loadPairedCamera(deviceId).then(setPairedCam);
  }, [deviceId]);

  if (pairedCam === undefined) return <Loading />;

  return (
    <div className="app-content">
      <div className="section-label" style={{ marginBottom: 10 }}>
        {deviceName}
      </div>

      {pairedCam ? (
        <DirectClips cam={pairedCam} />
      ) : (
        <GatewayClips deviceId={deviceId ?? ''} />
      )}
    </div>
  );
}
