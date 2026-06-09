//
// Shopping list: add, check off, delete.
//
import { useEffect, useState } from 'react';
import { EmptyState, Loading } from '../components/ui';
import { Icon } from '../components/icons';
import { api } from '../api/client';
import type { ShoppingItemDTO } from '../api/contract';
import { useApp } from '../state/store';

export function ShoppingScreen() {
  const [items, setItems] = useState<ShoppingItemDTO[] | null>(null);
  const [draft, setDraft] = useState('');
  const pushToast = useApp((s) => s.pushToast);

  useEffect(() => {
    api
      .shopping()
      .then(setItems)
      .catch((e) => {
        setItems([]);
        pushToast('bad', `Couldn't load list: ${(e as Error).message}`);
      });
  }, [pushToast]);

  async function add() {
    const item = draft.trim();
    if (!item) return;
    setDraft('');
    try {
      const created = await api.addShopping({ item });
      setItems((prev) => [...(prev ?? []), created]);
    } catch (e) {
      pushToast('bad', `Couldn't add: ${(e as Error).message}`);
    }
  }

  async function toggle(it: ShoppingItemDTO) {
    try {
      const updated = await api.toggleShopping(it.id, !it.done);
      setItems((prev) => prev?.map((x) => (x.id === it.id ? updated : x)) ?? prev);
    } catch (e) {
      pushToast('bad', `Couldn't update: ${(e as Error).message}`);
    }
  }

  async function remove(id: number) {
    try {
      await api.deleteShopping(id);
      setItems((prev) => prev?.filter((x) => x.id !== id) ?? prev);
    } catch (e) {
      pushToast('bad', `Couldn't remove: ${(e as Error).message}`);
    }
  }

  return (
    <div className="app-content">
      <div style={{ display: 'flex', gap: 8, marginBottom: 16 }}>
        <input
          className="input"
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          onKeyDown={(e) => e.key === 'Enter' && add()}
          placeholder="Add an item…"
        />
        <button
          className="btn"
          style={{ padding: '0 14px' }}
          onClick={add}
          aria-label="Add item"
        >
          <Icon name="plus" size={20} />
        </button>
      </div>

      {items === null ? (
        <Loading />
      ) : items.length === 0 ? (
        <EmptyState
          icon={<Icon name="shopping" size={34} />}
          title="List is empty"
          hint="Add items here or just ask the assistant."
        />
      ) : (
        items.map((it) => (
          <div className="row" key={it.id}>
            <button
              onClick={() => toggle(it)}
              aria-label="Toggle done"
              style={{
                width: 24,
                height: 24,
                borderRadius: 6,
                flex: '0 0 auto',
                border: '1px solid var(--line)',
                background: it.done ? 'var(--good)' : 'transparent',
                color: '#06281b',
                display: 'grid',
                placeItems: 'center',
                cursor: 'pointer',
              }}
            >
              {it.done && <Icon name="check" size={16} />}
            </button>
            <span
              style={{
                flex: 1,
                textDecoration: it.done ? 'line-through' : 'none',
                color: it.done ? 'var(--text-faint)' : 'var(--text)',
              }}
            >
              {it.item}
              {it.quantity ? ` · ${it.quantity}` : ''}
            </span>
            <button
              className="btn ghost"
              style={{ minHeight: 'auto', padding: 4 }}
              onClick={() => remove(it.id)}
              aria-label="Delete item"
            >
              <Icon name="trash" size={18} />
            </button>
          </div>
        ))
      )}
    </div>
  );
}
