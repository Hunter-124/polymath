//
// Long-term memory: store a note, and semantically search what's been stored.
//
import { useState } from 'react';
import { EmptyState, relativeTime } from '../components/ui';
import { Icon } from '../components/icons';
import { api } from '../api/client';
import type { MemoryDTO } from '../api/contract';
import { useApp } from '../state/store';

export function MemoryScreen() {
  const [q, setQ] = useState('');
  const [results, setResults] = useState<MemoryDTO[] | null>(null);
  const [searching, setSearching] = useState(false);
  const [note, setNote] = useState('');
  const pushToast = useApp((s) => s.pushToast);

  async function search() {
    const query = q.trim();
    if (!query) return;
    setSearching(true);
    try {
      setResults(await api.searchMemory(query));
    } catch (e) {
      pushToast('bad', `Search failed: ${(e as Error).message}`);
    } finally {
      setSearching(false);
    }
  }

  async function remember() {
    const text = note.trim();
    if (!text) return;
    try {
      await api.addMemory(text);
      setNote('');
      pushToast('good', 'Saved to memory');
    } catch (e) {
      pushToast('bad', `Couldn't save: ${(e as Error).message}`);
    }
  }

  return (
    <div className="app-content">
      <div className="card form-card">
        <div className="section-label" style={{ margin: '0 0 8px' }}>
          Remember something
        </div>
        <div className="inline-form" style={{ marginBottom: 0 }}>
          <input
            className="input"
            value={note}
            onChange={(e) => setNote(e.target.value)}
            onKeyDown={(e) => e.key === 'Enter' && remember()}
            placeholder="e.g. The spare key is under the third pot"
            aria-label="Memory note"
          />
          <button
            className="btn icon"
            onClick={remember}
            disabled={!note.trim()}
            aria-label="Save note"
          >
            <Icon name="plus" size={20} />
          </button>
        </div>
      </div>

      <div className="inline-form">
        <input
          className="input"
          value={q}
          onChange={(e) => setQ(e.target.value)}
          onKeyDown={(e) => e.key === 'Enter' && search()}
          placeholder="Search memory…"
          aria-label="Search memory"
        />
        <button
          className="btn secondary icon"
          onClick={search}
          disabled={searching || !q.trim()}
          aria-label="Search memory"
        >
          <Icon name="search" size={20} />
        </button>
      </div>

      {searching ? (
        <div className="empty">
          <div className="spinner" />
        </div>
      ) : results === null ? (
        <EmptyState
          icon={<Icon name="memory" size={34} />}
          title="Search long-term memory"
          hint="Notes, facts, and daily summaries the assistant has stored."
        />
      ) : results.length === 0 ? (
        <EmptyState title="No matches" />
      ) : (
        <div className="stack">
        {results.map((m) => (
          <div className="row" key={m.id} style={{ alignItems: 'flex-start' }}>
            <div style={{ flex: 1 }}>
              <div>{m.text}</div>
              <div className="row-subtitle">
                {m.kind}
                {m.source ? ` · ${m.source}` : ''} · {relativeTime(m.ts)}
              </div>
            </div>
            {typeof m.score === 'number' && (
              <span className="pill">{Math.round(m.score * 100)}%</span>
            )}
          </div>
        ))}
        </div>
      )}
    </div>
  );
}
