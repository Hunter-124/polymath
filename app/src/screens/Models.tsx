//
// Local model runtime view. Read-only on mobile: file registration still belongs
// on the desktop, but phones should be able to inspect what is actually loaded.
//
import { useCallback, useEffect, useMemo, useState } from 'react';
import { EmptyState, Loading } from '../components/ui';
import { Icon } from '../components/icons';
import { api } from '../api/client';
import type { ModelDTO } from '../api/contract';
import { useApp } from '../state/store';

const ROLE_ORDER: ModelDTO['role'][] = ['fast', 'heavy', 'vision', 'embedding'];
const ROLE_LABEL: Record<ModelDTO['role'], string> = {
  fast: 'Fast',
  heavy: 'Heavy',
  vision: 'Vision',
  embedding: 'Embedding',
};

function fileName(path: string) {
  const parts = path.split(/[\\/]/);
  return parts[parts.length - 1] || path;
}

function ModelRow({ model }: { model: ModelDTO }) {
  return (
    <div className={`row${model.loaded ? ' selected' : ''}`} style={{ alignItems: 'flex-start' }}>
      <span
        className={`dot${model.loaded ? ' good' : model.active ? ' warn' : ''}`}
        style={{ marginTop: 7 }}
      />
      <div style={{ flex: 1, minWidth: 0 }}>
        <div className="row-title" style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
          <span>{model.display_name || model.id}</span>
          {model.loaded && <span className="pill good">loaded</span>}
          {!model.loaded && model.active && <span className="pill">active</span>}
        </div>
        <div className="row-subtitle">
          ctx {model.n_ctx.toLocaleString()} · requested GPU {model.n_gpu_layers}
        </div>
        {model.loaded && (
          <div className="row-subtitle" style={{ color: 'var(--good)' }}>
            GPU {model.loaded_gpu_layers} · {model.footprint_mib.toLocaleString()} MiB
          </div>
        )}
        <div className="faint" style={{ fontSize: 11, marginTop: 4, wordBreak: 'break-all' }}>
          {fileName(model.path)}
        </div>
      </div>
    </div>
  );
}

export function ModelsScreen() {
  const [models, setModels] = useState<ModelDTO[] | null>(null);
  const pushToast = useApp((s) => s.pushToast);

  const load = useCallback(() => {
    api
      .models()
      .then(setModels)
      .catch((e) => {
        setModels([]);
        pushToast('bad', `Couldn't load models: ${(e as Error).message}`);
      });
  }, [pushToast]);

  useEffect(() => {
    load();
  }, [load]);

  const byRole = useMemo(() => {
    const grouped = new Map<ModelDTO['role'], ModelDTO[]>();
    for (const role of ROLE_ORDER) grouped.set(role, []);
    for (const model of models ?? []) grouped.get(model.role)?.push(model);
    for (const rows of grouped.values()) {
      rows.sort((a, b) => Number(b.loaded) - Number(a.loaded)
        || Number(b.active) - Number(a.active)
        || a.display_name.localeCompare(b.display_name));
    }
    return grouped;
  }, [models]);

  return (
    <div className="app-content">
      <div className="toolbar">
        <button className="btn ghost icon" onClick={load} aria-label="Refresh models">
          <Icon name="refresh" size={16} />
        </button>
      </div>

      {models === null ? (
        <Loading />
      ) : models.length === 0 ? (
        <EmptyState
          icon={<Icon name="models" size={34} />}
          title="No models registered"
          hint="Add local GGUF models from the desktop Model Manager."
        />
      ) : (
        ROLE_ORDER.map((role) => {
          const rows = byRole.get(role) ?? [];
          if (rows.length === 0) return null;
          return (
            <section key={role} style={{ marginBottom: 18 }}>
              <div className="section-label">{ROLE_LABEL[role]}</div>
              <div className="stack">
                {rows.map((model) => (
                  <ModelRow key={model.id} model={model} />
                ))}
              </div>
            </section>
          );
        })
      )}
    </div>
  );
}
