//
// Personality switcher. Tap a persona to make it active.
//
import { useEffect, useState } from 'react';
import { EmptyState, Loading } from '../components/ui';
import { Icon } from '../components/icons';
import { api } from '../api/client';
import type { PersonalityDTO } from '../api/contract';
import { useApp } from '../state/store';

export function PersonalitiesScreen() {
  const [items, setItems] = useState<PersonalityDTO[] | null>(null);
  const pushToast = useApp((s) => s.pushToast);
  const status = useApp((s) => s.status);
  const activeName = status?.active_personality;

  useEffect(() => {
    api
      .personalities()
      .then(setItems)
      .catch((e) => {
        setItems([]);
        pushToast('bad', `Couldn't load personalities: ${(e as Error).message}`);
      });
  }, [pushToast]);

  async function choose(name: string) {
    try {
      await api.setPersonality(name);
      setItems((prev) => prev?.map((p) => ({ ...p, active: p.name === name })) ?? prev);
    } catch (e) {
      pushToast('bad', `Couldn't switch: ${(e as Error).message}`);
    }
  }

  return (
    <div className="app-content">
      {items === null ? (
        <Loading />
      ) : items.length === 0 ? (
        <EmptyState
          icon={<Icon name="personalities" size={34} />}
          title="No personalities"
          hint="Drop persona bundles into the desktop's personalities folder."
        />
      ) : (
        items.map((p) => {
          const active = p.active || p.name === activeName;
          return (
            <div
              className="row"
              key={p.name}
              role="button"
              onClick={() => choose(p.name)}
              style={{ borderColor: active ? 'var(--accent)' : 'var(--line)' }}
            >
              <div style={{ flex: 1 }}>
                <div style={{ fontWeight: 600 }}>{p.name}</div>
                <div className="faint">
                  {[p.voice && `voice: ${p.voice}`, p.wake_phrase && `“${p.wake_phrase}”`]
                    .filter(Boolean)
                    .join(' · ')}
                </div>
              </div>
              {active && (
                <span style={{ color: 'var(--accent)' }}>
                  <Icon name="check" size={20} />
                </span>
              )}
            </div>
          );
        })
      )}
    </div>
  );
}
