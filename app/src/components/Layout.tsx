//
// App chrome: a sticky header (title + live-connection dot) and a bottom tab
// bar. Secondary screens (reached from "More") show a back chevron.
//
import { NavLink, Outlet, useLocation, useNavigate } from 'react-router-dom';
import { Icon, type IconName } from './icons';
import { useApp } from '../state/store';

const TABS: { to: string; label: string; icon: IconName }[] = [
  { to: '/chat', label: 'Chat', icon: 'chat' },
  { to: '/cameras', label: 'Cameras', icon: 'camera' },
  { to: '/tasks', label: 'Tasks', icon: 'tasks' },
  { to: '/timeline', label: 'Timeline', icon: 'timeline' },
  { to: '/more', label: 'More', icon: 'more' },
];

const TITLES: Record<string, string> = {
  '/chat': 'Chat',
  '/cameras': 'Cameras',
  '/tasks': 'Task Queue',
  '/timeline': 'Timeline',
  '/more': 'More',
  '/shopping': 'Shopping',
  '/reminders': 'Reminders',
  '/memory': 'Memory',
  '/personalities': 'Personalities',
  '/settings': 'Settings & Privacy',
  '/devices': 'Paired Devices',
};

export function Layout() {
  const { pathname } = useLocation();
  const navigate = useNavigate();
  const online = useApp((s) => s.online);
  const remote = useApp((s) => s.remote);

  const isPrimary = TABS.some((t) => t.to === pathname);
  const title = TITLES[pathname] ?? 'Polymath';

  return (
    <div className="app-shell">
      <header className="app-header">
        {!isPrimary && (
          <button
            className="btn ghost"
            style={{ padding: 4, minHeight: 'auto' }}
            onClick={() => navigate(-1)}
            aria-label="Back"
          >
            <Icon name="back" size={22} />
          </button>
        )}
        <h1>{title}</h1>
        {remote && <span className="pill">relay</span>}
        <span
          title={online ? 'connected' : 'offline'}
          style={{
            width: 9,
            height: 9,
            borderRadius: '50%',
            background: online ? 'var(--good)' : 'var(--text-faint)',
          }}
        />
      </header>

      <main className="app-body">
        <Outlet />
      </main>

      <nav className="tabbar">
        {TABS.map((t) => (
          <NavLink
            key={t.to}
            to={t.to}
            className={({ isActive }) => (isActive ? 'active' : '')}
          >
            <Icon name={t.icon} size={22} />
            {t.label}
          </NavLink>
        ))}
      </nav>
    </div>
  );
}
