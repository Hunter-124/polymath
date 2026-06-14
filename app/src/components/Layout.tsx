//
// App chrome: a sticky header (title + live-connection dot) and a bottom tab
// bar. Secondary screens (reached from "More") show a back chevron.
//
import { NavLink, Outlet, useLocation, useNavigate } from 'react-router-dom';
import { Icon, type IconName } from './icons';
import { ErrorBoundary } from './ErrorBoundary';
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
  '/models': 'Models',
  '/devices': 'Paired Devices',
  '/fabric': 'Edge Devices',
  '/instruments': 'Instruments',
  '/lab': 'Lab Sessions',
};

export function Layout() {
  const { pathname } = useLocation();
  const navigate = useNavigate();
  const online = useApp((s) => s.online);
  const remote = useApp((s) => s.remote);

  const isPrimary = TABS.some((t) => t.to === pathname);
  const title =
    TITLES[pathname] ??
    (pathname.startsWith('/clips/') ? 'Camera Clips' : 'Hearth');

  return (
    <div className="app-shell">
      <header className="app-header">
        {!isPrimary && (
          <button
            className="btn ghost icon"
            onClick={() => navigate(-1)}
            aria-label="Back"
          >
            <Icon name="back" size={22} />
          </button>
        )}
        <h1>{title}</h1>
        {remote && <span className="pill">relay</span>}
        <span
          className={`dot${online ? ' good' : ''}`}
          title={online ? 'connected' : 'offline'}
          aria-label={online ? 'Connected' : 'Offline'}
        />
      </header>

      <main className="app-body">
        <ErrorBoundary resetKey={pathname}>
          <Outlet />
        </ErrorBoundary>
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
