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
  { to: '/settings', label: 'Settings & privacy', icon: 'settings' },
  { to: '/devices', label: 'Paired devices', icon: 'devices' },
];

export function MoreScreen() {
  const status = useApp((s) => s.status);

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
        <div className="faint">Assistant</div>
        <div style={{ fontWeight: 600, marginTop: 2 }}>
          {status?.active_personality ?? '—'}
        </div>
        <div className="faint" style={{ marginTop: 6 }}>
          {status?.model_status ?? 'status unknown'}
        </div>
      </div>

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

      <div style={{ marginTop: 24 }}>
        <Button variant="danger" block onClick={doUnpair}>
          Unpair this device
        </Button>
      </div>
    </div>
  );
}
