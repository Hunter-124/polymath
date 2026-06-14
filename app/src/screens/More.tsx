//
// "More" hub: assistant status, links to secondary screens, and unpair.
//
import { Link } from 'react-router-dom';
import { Button } from '../components/ui';
import { Icon, type IconName } from '../components/icons';
import { unpair } from '../api/auth';
import { useApp } from '../state/store';

const LINKS: { to: string; label: string; icon: IconName }[] = [
  { to: '/shopping', label: 'Shopping list', icon: 'shopping' },
  { to: '/reminders', label: 'Reminders', icon: 'reminders' },
  { to: '/memory', label: 'Memory', icon: 'memory' },
  { to: '/personalities', label: 'Personalities', icon: 'personalities' },
  { to: '/models', label: 'Models', icon: 'models' },
  { to: '/settings', label: 'Settings & privacy', icon: 'settings' },
  { to: '/devices', label: 'Paired devices', icon: 'devices' },
  { to: '/fabric', label: 'Edge devices', icon: 'fabric' },
  { to: '/instruments', label: 'Instruments', icon: 'instrument' },
  { to: '/lab', label: 'Lab sessions', icon: 'lab' },
];

export function MoreScreen() {
  const status = useApp((s) => s.status);
  const online = useApp((s) => s.online);
  const remote = useApp((s) => s.remote);

  async function doUnpair() {
    if (
      !confirm(
        'Unpair this device? You will need to scan the QR again to reconnect.',
      )
    )
      return;
    await unpair();
    location.reload();
  }

  return (
    <div className="app-content">
      <div className="card" style={{ marginBottom: 16 }}>
        <div style={{ display: 'flex', gap: 12, alignItems: 'center' }}>
          <span className={`dot${online ? ' good' : ''}`} />
          <div style={{ flex: 1, minWidth: 0 }}>
            <div className="faint">Assistant</div>
            <div className="row-title" style={{ marginTop: 2 }}>
              {status?.active_personality ?? 'No personality loaded'}
            </div>
            <div className="row-subtitle">
              {status?.model_status ?? 'status unknown'}
            </div>
            <div
              className="row-subtitle"
              style={{ color: status?.tts_ready === false ? 'var(--warn)' : undefined }}
            >
              {status?.tts_status ?? 'TTS status unknown'}
            </div>
          </div>
          <span className="pill">{remote ? 'relay' : 'local'}</span>
        </div>
      </div>

      <div className="stack">
        {LINKS.map((l) => (
          <Link key={l.to} to={l.to} style={{ color: 'inherit' }}>
            <div className="row">
              <span style={{ color: 'var(--accent)' }}>
                <Icon name={l.icon} size={20} />
              </span>
              <span style={{ flex: 1 }}>{l.label}</span>
              <span className="faint">›</span>
            </div>
          </Link>
        ))}
      </div>

      <div style={{ marginTop: 24 }}>
        <Button variant="danger" block onClick={doUnpair}>
          Unpair this device
        </Button>
      </div>
    </div>
  );
}
