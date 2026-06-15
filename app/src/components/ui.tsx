//
// Shared presentational atoms. Keep these dependency-free and styled via the
// classes in theme/theme.css so every screen looks consistent.
//
import type { ReactNode } from 'react';

export function Spinner() {
  return <div className="spinner" aria-label="loading" />;
}

export function Loading({ label }: { label?: string }) {
  return (
    <div className="empty">
      <Spinner />
      {label && <div style={{ marginTop: 10 }}>{label}</div>}
    </div>
  );
}

export function EmptyState({
  icon,
  title,
  hint,
}: {
  icon?: ReactNode;
  title: string;
  hint?: string;
}) {
  return (
    <div className="empty">
      {icon && <div style={{ fontSize: 34, marginBottom: 8 }}>{icon}</div>}
      <div style={{ fontWeight: 600, color: 'var(--text-dim)' }}>{title}</div>
      {hint && <div className="faint" style={{ marginTop: 6 }}>{hint}</div>}
    </div>
  );
}

export function Card({
  children,
  className = '',
}: {
  children: ReactNode;
  className?: string;
}) {
  return <div className={`card ${className}`}>{children}</div>;
}

export function Row({
  children,
  onClick,
}: {
  children: ReactNode;
  onClick?: () => void;
}) {
  return (
    <div className="row" onClick={onClick} role={onClick ? 'button' : undefined}>
      {children}
    </div>
  );
}

export function Button({
  children,
  variant = 'primary',
  block,
  ...rest
}: {
  children: ReactNode;
  variant?: 'primary' | 'secondary' | 'ghost' | 'danger';
  block?: boolean;
} & React.ButtonHTMLAttributes<HTMLButtonElement>) {
  const cls = variant === 'primary' ? 'btn' : `btn ${variant}`;
  return (
    <button className={`${cls}${block ? ' block' : ''}`} {...rest}>
      {children}
    </button>
  );
}

export function Toggle({
  checked,
  onChange,
  label,
}: {
  checked: boolean;
  onChange: (v: boolean) => void;
  label?: string;
}) {
  return (
    <button
      role="switch"
      aria-checked={checked}
      aria-label={label}
      onClick={() => onChange(!checked)}
      style={{
        width: 46,
        height: 28,
        borderRadius: 999,
        border: '1px solid var(--line)',
        background: checked ? 'var(--accent)' : 'var(--bg-elev-2)',
        position: 'relative',
        transition: 'background .15s ease',
        flex: '0 0 auto',
      }}
    >
      <span
        style={{
          position: 'absolute',
          top: 2,
          left: checked ? 20 : 2,
          width: 22,
          height: 22,
          borderRadius: '50%',
          background: '#fff',
          transition: 'left .15s ease',
        }}
      />
    </button>
  );
}

export function relativeTime(unixSeconds: number): string {
  const diff = Date.now() / 1000 - unixSeconds;
  if (diff < 60) return 'just now';
  if (diff < 3600) return `${Math.floor(diff / 60)}m ago`;
  if (diff < 86400) return `${Math.floor(diff / 3600)}h ago`;
  return `${Math.floor(diff / 86400)}d ago`;
}
