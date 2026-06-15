//
// Deep-work task queue: see queued/running/done jobs, queue a new one, cancel.
// Live-updates from `task` EventBus events.
//
import { useCallback, useEffect, useState } from 'react';
import { Button, EmptyState, Loading } from '../components/ui';
import { Icon } from '../components/icons';
import { api } from '../api/client';
import { socket } from '../api/socket';
import type { TaskDTO, TaskStatus } from '../api/contract';
import { useApp } from '../state/store';

const TASK_TYPES = ['research', 'lab_report', 'summary'] as const;
type TaskType = (typeof TASK_TYPES)[number];

function statusClass(s: TaskStatus): string {
  if (s === 'done') return 'pill good';
  if (s === 'error') return 'pill bad';
  if (s === 'canceled') return 'pill';
  return 'pill warn'; // queued | running
}

const STATUS_ORDER: Record<TaskStatus, number> = {
  running: 0,
  queued: 1,
  error: 2,
  done: 3,
  canceled: 4,
};

export function TasksScreen() {
  const [tasks, setTasks] = useState<TaskDTO[] | null>(null);
  const [type, setType] = useState<TaskType>('research');
  const [prompt, setPrompt] = useState('');
  const [busy, setBusy] = useState(false);
  const pushToast = useApp((s) => s.pushToast);

  const load = useCallback(() => {
    api
      .tasks()
      .then(setTasks)
      .catch((e) => {
        setTasks([]);
        pushToast('bad', `Couldn't load tasks: ${(e as Error).message}`);
      });
  }, [pushToast]);

  useEffect(() => {
    load();
  }, [load]);

  // Live status updates.
  useEffect(() => {
    socket.subscribe(['task']);
    const off = socket.on((e) => {
      if (e.type !== 'task') return;
      const d = e.data as { task_id: number; status: TaskStatus };
      setTasks((prev) => {
        if (!prev) return prev;
        const idx = prev.findIndex((t) => t.id === d.task_id);
        if (idx === -1) {
          load(); // a new task appeared
          return prev;
        }
        const next = [...prev];
        next[idx] = { ...next[idx], status: d.status };
        return next;
      });
    });
    return () => {
      off();
      socket.unsubscribe(['task']);
    };
  }, [load]);

  async function create() {
    if (busy) return;
    setBusy(true);
    try {
      await api.createTask({
        type,
        params: prompt.trim() ? { prompt: prompt.trim() } : {},
      });
      setPrompt('');
      load();
    } catch (e) {
      pushToast('bad', `Couldn't queue task: ${(e as Error).message}`);
    } finally {
      setBusy(false);
    }
  }

  async function cancel(id: number) {
    try {
      await api.updateTask(id, { status: 'canceled' });
      setTasks(
        (prev) =>
          prev?.map((t) => (t.id === id ? { ...t, status: 'canceled' } : t)) ??
          prev,
      );
    } catch (e) {
      pushToast('bad', `Couldn't cancel: ${(e as Error).message}`);
    }
  }

  const sortedTasks = tasks
    ? [...tasks].sort(
        (a, b) =>
          STATUS_ORDER[a.status] - STATUS_ORDER[b.status] ||
          b.updated_at - a.updated_at,
      )
    : null;

  return (
    <div className="app-content">
      <div className="card form-card">
        <div className="section-label" style={{ margin: '0 0 8px' }}>
          Queue a deep-work task
        </div>
        <select
          className="input"
          value={type}
          onChange={(e) => setType(e.target.value as TaskType)}
          style={{ marginBottom: 8 }}
        >
          {TASK_TYPES.map((t) => (
            <option key={t} value={t}>
              {t.replace('_', ' ')}
            </option>
          ))}
        </select>
        <textarea
          className="input"
          value={prompt}
          onChange={(e) => setPrompt(e.target.value)}
          placeholder="Describe the task (optional)…"
          style={{ minHeight: 76 }}
        />
        <Button block disabled={busy} onClick={create}>
          <Icon name="plus" size={18} /> Add to queue
        </Button>
      </div>

      {sortedTasks === null ? (
        <Loading />
      ) : sortedTasks.length === 0 ? (
        <EmptyState
          icon={<Icon name="tasks" size={34} />}
          title="No tasks queued"
          hint="Heavy jobs run while the assistant is idle."
        />
      ) : (
        <div className="stack">
        {sortedTasks.map((t) => (
          <div className="row" key={t.id} style={{ alignItems: 'flex-start' }}>
            <div style={{ flex: 1 }}>
              <div className="row-title" style={{ textTransform: 'capitalize' }}>
                {t.type.replace('_', ' ')}
              </div>
              {typeof t.params?.prompt === 'string' && (
                <div className="row-subtitle">{t.params.prompt as string}</div>
              )}
            </div>
            <span className={statusClass(t.status)}>{t.status}</span>
            {(t.status === 'queued' || t.status === 'running') && (
              <button
                className="btn ghost icon"
                onClick={() => cancel(t.id)}
                aria-label="Cancel task"
              >
                <Icon name="trash" size={18} />
              </button>
            )}
          </div>
        ))
        }
        </div>
      )}
    </div>
  );
}
