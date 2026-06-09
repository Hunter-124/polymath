//
// Onboarding / pairing. Scan the QR shown in the desktop's Settings ▸ Mobile
// Access screen (or paste the code manually), name the device, done.
//
import { useEffect, useRef, useState } from 'react';
import { BrowserQRCodeReader, type IScannerControls } from '@zxing/browser';
import { Capacitor } from '@capacitor/core';
import { Button, Card } from '../components/ui';
import { Icon } from '../components/icons';
import { pair, parsePairingQR } from '../api/auth';

type Stage = 'intro' | 'scan' | 'manual' | 'pairing' | 'error';

function platform(): 'ios' | 'android' | 'web' {
  const p = Capacitor.getPlatform();
  return p === 'ios' || p === 'android' ? p : 'web';
}

function defaultName(): string {
  const p = platform();
  if (p === 'ios') return 'iPhone';
  if (p === 'android') return 'Android phone';
  return 'Web browser';
}

export function Pair({ onPaired }: { onPaired: () => void }) {
  const [stage, setStage] = useState<Stage>('intro');
  const [error, setError] = useState('');
  const [deviceName, setDeviceName] = useState(defaultName());
  const [manualText, setManualText] = useState('');
  const videoRef = useRef<HTMLVideoElement>(null);
  const controlsRef = useRef<IScannerControls | null>(null);

  // Start/stop the camera scanner with the scan stage.
  useEffect(() => {
    if (stage !== 'scan') return;
    let cancelled = false;
    const reader = new BrowserQRCodeReader();
    reader
      .decodeFromVideoDevice(undefined, videoRef.current!, (result, _err, controls) => {
        controlsRef.current = controls;
        if (result && !cancelled) {
          controls.stop();
          void doPair(result.getText());
        }
      })
      .catch((e) => {
        setError(`Camera unavailable: ${e?.message ?? e}. Enter the code manually.`);
        setStage('manual');
      });
    return () => {
      cancelled = true;
      controlsRef.current?.stop();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [stage]);

  async function doPair(qrText: string) {
    setStage('pairing');
    setError('');
    try {
      const payload = parsePairingQR(qrText);
      await pair(payload, deviceName.trim() || defaultName(), platform());
      onPaired();
    } catch (e) {
      setError((e as Error).message || 'Pairing failed.');
      setStage('error');
    }
  }

  return (
    <div className="app-content" style={{ paddingTop: 'calc(var(--safe-top) + 32px)' }}>
      <div style={{ textAlign: 'center', marginBottom: 24 }}>
        <div
          style={{
            width: 64,
            height: 64,
            margin: '0 auto 14px',
            borderRadius: 18,
            background: 'linear-gradient(135deg, var(--accent), var(--accent-2))',
            display: 'grid',
            placeItems: 'center',
            color: '#fff',
            fontWeight: 800,
            fontSize: 28,
          }}
        >
          H
        </div>
        <div className="title" style={{ marginBottom: 2 }}>
          Hearth
        </div>
        <div className="faint">Connect to your home assistant</div>
      </div>

      {stage === 'intro' && (
        <Card>
          <p className="muted" style={{ marginTop: 0 }}>
            On your desktop open <b>Settings ▸ Mobile Access</b>, turn on{' '}
            <b>Allow remote access</b>, and a pairing QR will appear.
          </p>
          <label className="faint">This device's name</label>
          <input
            className="input"
            style={{ margin: '6px 0 16px' }}
            value={deviceName}
            onChange={(e) => setDeviceName(e.target.value)}
            placeholder="e.g. George's iPhone"
          />
          <Button block onClick={() => setStage('scan')}>
            <Icon name="qr" size={20} /> Scan pairing QR
          </Button>
          <Button
            variant="ghost"
            block
            onClick={() => setStage('manual')}
            style={{ marginTop: 8 }}
          >
            Enter code manually
          </Button>
        </Card>
      )}

      {stage === 'scan' && (
        <Card>
          <div
            style={{
              position: 'relative',
              borderRadius: 12,
              overflow: 'hidden',
              aspectRatio: '1 / 1',
              background: '#000',
            }}
          >
            <video
              ref={videoRef}
              style={{ width: '100%', height: '100%', objectFit: 'cover' }}
              muted
              playsInline
            />
            <div
              style={{
                position: 'absolute',
                inset: 24,
                border: '2px solid rgba(255,255,255,.7)',
                borderRadius: 12,
              }}
            />
          </div>
          <p className="faint" style={{ textAlign: 'center' }}>
            Point the camera at the QR on your desktop.
          </p>
          <Button variant="ghost" block onClick={() => setStage('intro')}>
            Cancel
          </Button>
        </Card>
      )}

      {stage === 'manual' && (
        <Card>
          <label className="faint">
            Paste the pairing code (the text under the QR)
          </label>
          <textarea
            className="input"
            style={{ margin: '6px 0 14px', minHeight: 120, fontFamily: 'monospace' }}
            value={manualText}
            onChange={(e) => setManualText(e.target.value)}
            placeholder='{"relay_url":"…","home_id":"…","pair_code":"…"}'
          />
          <Button block disabled={!manualText.trim()} onClick={() => doPair(manualText)}>
            Pair this device
          </Button>
          <Button variant="ghost" block onClick={() => setStage('intro')} style={{ marginTop: 8 }}>
            Back
          </Button>
        </Card>
      )}

      {stage === 'pairing' && (
        <Card>
          <div className="empty">
            <div className="spinner" />
            <div style={{ marginTop: 12 }}>Pairing…</div>
          </div>
        </Card>
      )}

      {stage === 'error' && (
        <Card>
          <p style={{ color: 'var(--bad)', marginTop: 0 }}>{error}</p>
          <Button block onClick={() => setStage('intro')}>
            Try again
          </Button>
        </Card>
      )}
    </div>
  );
}
