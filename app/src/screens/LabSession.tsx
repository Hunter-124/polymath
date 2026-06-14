//
// Lab Sessions screen: lists LabSessionDTOs and shows step-by-step progress.
// Steps live-update from `lab_step` WS events piped through the Zustand store.
// A link to the generated report doc is shown when report_doc_id is set.
//
import { useCallback, useEffect, useState } from 'react';
import { EmptyState, Loading, relativeTime } from '../components/ui';
import { Icon } from '../components/icons';
import { api } from '../api/client';
import { socket } from '../api/socket';
import { useApp } from '../state/store';
import type { LabSessionDTO, LabStepDTO, LabSessionStatus } from '../api/contract';

const STATUS_COLOR: Record<LabSessionStatus, string> = {
  active: 'var(--accent)',
  paused: 'var(--text-dim)',
  done: 'var(--good)',
  canceled: 'var(--text-faint)',
};

function StepRow({
  step,
  stepKey,
}: {
  step: LabStepDTO;
  stepKey: string;
}) {
  const liveStep = useApp((s) => s.labSteps[stepKey]);
  const effective = liveStep ?? step;
  const isOut = effective.measured_value != null && !effective.verified;
  return (
    <div
      style={{
        display: 'flex',
        gap: 10,
        padding: '8px 0',
        borderBottom: '1px solid var(--line)',
      }}
    >
      <div
        style={{
          width: 22,
          height: 22,
          borderRadius: '50%',
          border: '2px solid',
          borderColor: effective.verified
            ? 'var(--good)'
            : isOut
              ? 'var(--danger, #e53e3e)'
              : 'var(--line)',
          display: 'grid',
          placeItems: 'center',
          flex: '0 0 auto',
          marginTop: 2,
          fontSize: 11,
          fontWeight: 700,
          color: effective.verified
            ? 'var(--good)'
            : isOut
              ? 'var(--danger, #e53e3e)'
              : 'var(--text-faint)',
        }}
      >
        {effective.verified ? <Icon name="check" size={12} /> : effective.step_no}
      </div>
      <div style={{ flex: 1 }}>
        <div style={{ fontWeight: 500 }}>{effective.prompt}</div>
        <div className="faint">
          Expected: {effective.expected_kind} in {effective.expected_unit}
          {effective.measured_value != null
            ? ` · Measured: ${effective.measured_value} ${effective.measured_unit}`
            : ''}
        </div>
        {isOut && (
          <span
            className="pill"
            style={{
              marginTop: 4,
              background: 'var(--danger, #e53e3e)',
              color: '#fff',
              borderColor: 'transparent',
            }}
          >
            out of range
          </span>
        )}
        {effective.verified && effective.verified_at && (
          <div className="faint" style={{ fontSize: 11 }}>
            Verified {relativeTime(effective.verified_at)}
          </div>
        )}
      </div>
    </div>
  );
}

function SessionDetail({ session }: { session: LabSessionDTO }) {
  const [detail, setDetail] = useState<LabSessionDTO | null>(null);
  const [loading, setLoading] = useState(false);
  const [open, setOpen] = useState(false);
  const pushToast = useApp((s) => s.pushToast);
  const upsertLabStep = useApp((s) => s.upsertLabStep);

  const loadDetail = useCallback(() => {
    setLoading(true);
    api
      .labSession(session.id)
      .then((s) => {
        setDetail(s);
        s.steps?.forEach((step) => upsertLabStep(session.id, step));
        setLoading(false);
      })
      .catch((e) => {
        setLoading(false);
        pushToast('bad', `Couldn't load session: ${(e as Error).message}`);
      });
  }, [session.id, pushToast, upsertLabStep]);

  function toggle() {
    setOpen((o) => !o);
    if (!open && !detail) loadDetail();
  }

  return (
    <div
      className="card"
      style={{ marginBottom: 10, cursor: 'pointer' }}
      onClick={toggle}
    >
      <div style={{ display: 'flex', gap: 10, alignItems: 'flex-start' }}>
        <span
          style={{
            width: 9,
            height: 9,
            borderRadius: '50%',
            marginTop: 6,
            flex: '0 0 auto',
            background: STATUS_COLOR[session.status],
          }}
        />
        <div style={{ flex: 1 }}>
          <div style={{ fontWeight: 600 }}>{session.title}</div>
          <div className="faint">{session.objective}</div>
          <div className="faint" style={{ marginTop: 4 }}>
            {session.status} · started {relativeTime(session.started_at)}
            {session.ended_at
              ? ` · ended ${relativeTime(session.ended_at)}`
              : ''}
          </div>
        </div>
        <span className="faint">{open ? '▲' : '▼'}</span>
      </div>

      {open && (
        <div style={{ marginTop: 12 }}>
          {loading && <div className="spinner" />}

          {detail?.report_doc_id != null && (
            <div
              className="faint"
              style={{
                display: 'flex',
                gap: 6,
                alignItems: 'center',
                marginBottom: 8,
                fontSize: 13,
              }}
            >
              <Icon name="clip" size={14} />
              Report doc #{detail.report_doc_id}
            </div>
          )}

          {detail?.steps?.map((step) => (
            <StepRow
              key={step.step_no}
              step={step}
              stepKey={`${session.id}:${step.step_no}`}
            />
          ))}

          {detail && (!detail.steps || detail.steps.length === 0) && (
            <div className="faint" style={{ textAlign: 'center', padding: 12 }}>
              No steps recorded yet.
            </div>
          )}
        </div>
      )}
    </div>
  );
}

export function LabSessionScreen() {
  const [sessions, setSessions] = useState<LabSessionDTO[] | null>(null);
  const pushToast = useApp((s) => s.pushToast);

  const load = useCallback(() => {
    api
      .labSessions()
      .then(setSessions)
      .catch((e) => {
        setSessions([]);
        pushToast('bad', `Couldn't load lab sessions: ${(e as Error).message}`);
      });
  }, [pushToast]);

  useEffect(() => {
    load();
  }, [load]);

  // Subscribe to lab_step events so open session details update live.
  useEffect(() => {
    socket.subscribe(['lab_step']);
    return () => socket.unsubscribe(['lab_step']);
  }, []);

  return (
    <div className="app-content">
      <div
        style={{ display: 'flex', justifyContent: 'flex-end', marginBottom: 10 }}
      >
        <button className="btn ghost" style={{ padding: '0 12px', minHeight: 36 }} onClick={load}>
          <Icon name="refresh" size={16} />
        </button>
      </div>

      {sessions === null ? (
        <Loading />
      ) : sessions.length === 0 ? (
        <EmptyState
          icon={<Icon name="lab" size={34} />}
          title="No lab sessions"
          hint="Start a guided experiment from the desktop to see it here."
        />
      ) : (
        sessions.map((s) => <SessionDetail key={s.id} session={s} />)
      )}
    </div>
  );
}
