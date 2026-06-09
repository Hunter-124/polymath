//
// Settings & privacy. Privacy toggles, retention, behavior, and web-search
// config — each change is persisted through the gateway to the settings table.
//
import { useEffect, useState } from 'react';
import { Loading, Toggle } from '../components/ui';
import { api } from '../api/client';
import { useApp } from '../state/store';

const PRIVACY_LABELS: Record<string, string> = {
  'privacy.mic_enabled': 'Microphone',
  'privacy.ambient_transcription': 'Ambient transcription',
  'privacy.face_recognition': 'Face recognition',
  'privacy.cameras_enabled': 'Cameras',
  'privacy.encrypt_at_rest': 'Encrypt data at rest',
};

export function SettingsScreen() {
  const [settings, setSettings] = useState<Record<string, string> | null>(null);
  const pushToast = useApp((s) => s.pushToast);

  useEffect(() => {
    api
      .settings()
      .then(setSettings)
      .catch((e) => {
        setSettings({});
        pushToast('bad', `Couldn't load settings: ${(e as Error).message}`);
      });
  }, [pushToast]);

  const setLocal = (key: string, value: string) =>
    setSettings((prev) => (prev ? { ...prev, [key]: value } : prev));

  async function save(key: string, value: string) {
    setLocal(key, value);
    try {
      await api.patchSetting({ key, value });
    } catch (e) {
      pushToast('bad', `Couldn't save: ${(e as Error).message}`);
    }
  }

  if (settings === null) return <Loading />;
  const s = settings;
  const get = (k: string, def = '') => s[k] ?? def;
  const privacyKeys = Object.keys(PRIVACY_LABELS).filter((k) => k in s);

  return (
    <div className="app-content">
      <div className="section-label">Privacy</div>
      {privacyKeys.map((k) => (
        <div className="row" key={k}>
          <span style={{ flex: 1 }}>{PRIVACY_LABELS[k]}</span>
          <Toggle checked={get(k) === 'true'} onChange={(v) => save(k, String(v))} />
        </div>
      ))}

      <div className="section-label">Retention (days · 0 = keep forever)</div>
      <div className="row">
        <span style={{ flex: 1 }}>Ambient transcripts</span>
        <input
          className="input"
          type="number"
          style={{ width: 90 }}
          value={get('retention.ambient_days', '7')}
          onChange={(e) => setLocal('retention.ambient_days', e.target.value)}
          onBlur={(e) => save('retention.ambient_days', e.target.value)}
        />
      </div>
      <div className="row">
        <span style={{ flex: 1 }}>Events</span>
        <input
          className="input"
          type="number"
          style={{ width: 90 }}
          value={get('retention.events_days', '30')}
          onChange={(e) => setLocal('retention.events_days', e.target.value)}
          onBlur={(e) => save('retention.events_days', e.target.value)}
        />
      </div>

      <div className="section-label">Behavior</div>
      <div className="row">
        <span style={{ flex: 1 }}>Quiet hours start</span>
        <input
          className="input"
          style={{ width: 110 }}
          value={get('behavior.quiet_start', '22:00')}
          onChange={(e) => setLocal('behavior.quiet_start', e.target.value)}
          onBlur={(e) => save('behavior.quiet_start', e.target.value)}
        />
      </div>
      <div className="row">
        <span style={{ flex: 1 }}>Quiet hours end</span>
        <input
          className="input"
          style={{ width: 110 }}
          value={get('behavior.quiet_end', '07:00')}
          onChange={(e) => setLocal('behavior.quiet_end', e.target.value)}
          onBlur={(e) => save('behavior.quiet_end', e.target.value)}
        />
      </div>
      <div className="row">
        <span style={{ flex: 1 }}>Wake word</span>
        <input
          className="input"
          style={{ width: 150 }}
          value={get('audio.wake_word')}
          onChange={(e) => setLocal('audio.wake_word', e.target.value)}
          onBlur={(e) => save('audio.wake_word', e.target.value)}
        />
      </div>

      <div className="section-label">Web search</div>
      <div className="row">
        <span style={{ flex: 1 }}>Backend</span>
        <select
          className="input"
          style={{ width: 140 }}
          value={get('web.search_backend', 'ddg')}
          onChange={(e) => save('web.search_backend', e.target.value)}
        >
          <option value="searxng">SearXNG</option>
          <option value="brave">Brave</option>
          <option value="ddg">DuckDuckGo</option>
        </select>
      </div>
      <div className="row">
        <span style={{ flex: 1 }}>API key</span>
        <input
          className="input"
          type="password"
          style={{ width: 160 }}
          value={get('web.search_api_key')}
          onChange={(e) => setLocal('web.search_api_key', e.target.value)}
          onBlur={(e) => save('web.search_api_key', e.target.value)}
          placeholder="—"
        />
      </div>
    </div>
  );
}
