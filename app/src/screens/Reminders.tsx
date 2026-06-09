//
// Reminders: time-based (due_at) or condition-based. Add and delete.
//
import { useEffect, useState } from 'react';
import { Button, EmptyState, Loading } from '../components/ui';
import { Icon } from '../components/icons';
import { api } from '../api/client';
import type { ReminderDTO } from '../api/contract';
import { useApp } from '../state/store';

function fmtDue(due?: number): string {
  if (!due) return '';
  return new Date(due * 1000).toLocaleString([], {
    month: 'short',
    day: 'numeric',
    hour: 'numeric',
    minute: '2-digit',
  });
}

export function RemindersScreen() {
  const [items, setItems] = useState<ReminderDTO[] | null>(null);
  const [text, setText] = useState('');
  const [when, setWhen] = useState('');
  const pushToast = useApp((s) => s.pushToast);

  useEffect(() => {
    api
      .reminders()
      .then(setItems)
      .catch((e) => {
        setItems([]);
        pushToast('bad', `Couldn't load reminders: ${(e as Error).message}`);
      });
  }, [pushToast]);

  async function add() {
    const t = text.trim();
    if (!t) return;
    const due_at = when ? Math.floor(new Date(when).getTime() / 1000) : undefined;
    try {
      const created = await api.createReminder({ text: t, due_at });
      setItems((prev) => [created, ...(prev ?? [])]);
      setText('');
      setWhen('');
    } catch (e) {
      pushToast('bad', `Couldn't add: ${(e as Error).message}`);
    }
  }

  async function remove(id: number) {
    try {
      await api.deleteReminder(id);
      setItems((prev) => prev?.filter((x) => x.id !== id) ?? prev);
    } catch (e) {
      pushToast('bad', `Couldn't delete: ${(e as Error).message}`);
    }
  }

  return (
    <div className="app-content">
      <div className="card" style={{ marginBottom: 16 }}>
        <input
          className="input"
          value={text}
          onChange={(e) => setText(e.target.value)}
          placeholder="Remind me to…"
          style={{ marginBottom: 8 }}
        />
        <input
          className="input"
          type="datetime-local"
          value={when}
          onChange={(e) => setWhen(e.target.value)}
          style={{ marginBottom: 8 }}
        />
        <Button block onClick={add}>
          <Icon name="plus" size={18} /> Add reminder
        </Button>
      </div>

      {items === null ? (
        <Loading />
      ) : items.length === 0 ? (
        <EmptyState
          icon={<Icon name="reminders" size={34} />}
          title="No reminders"
          hint="Time-based or condition-based — ask the assistant or add one here."
        />
      ) : (
        items.map((r) => (
          <div className="row" key={r.id}>
            <div style={{ flex: 1 }}>
              <div style={{ fontWeight: 600 }}>{r.text}</div>
              <div className="faint">
                {r.due_at
                  ? fmtDue(r.due_at)
                  : r.condition
                    ? `when ${r.condition}`
                    : 'no schedule'}
                {r.rrule ? ' · repeats' : ''}
              </div>
            </div>
            {r.fired && <span className="pill">done</span>}
            <button
              className="btn ghost"
              style={{ minHeight: 'auto', padding: 4 }}
              onClick={() => remove(r.id)}
              aria-label="Delete reminder"
            >
              <Icon name="trash" size={18} />
            </button>
          </div>
        ))
      )}
    </div>
  );
}
