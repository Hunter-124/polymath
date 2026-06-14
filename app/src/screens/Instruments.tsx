//
// Instruments screen: lists InstrumentDTOs with their latest readings.
// Readings are polled on mount and updated live via `instrument_reading` WS
// events piped through the Zustand store.
//
import { useCallback, useEffect, useState } from 'react';
import { EmptyState, Loading } from '../components/ui';
import { Icon } from '../components/icons';
import { api } from '../api/client';
import { socket } from '../api/socket';
import { useApp } from '../state/store';
import type { InstrumentDTO } from '../api/contract';

function formatValue(v: number | null | undefined, unit: string): string {
  if (v == null) return '—';
  return `${v.toFixed(3).replace(/\.?0+$/, '')} ${unit}`;
}

function InRangePill({ inRange }: { inRange: boolean | undefined }) {
  if (inRange === undefined) return null;
  return (
    <span
      className={`pill${inRange ? ' good' : ''}`}
      style={
        inRange
          ? {}
          : { background: 'var(--danger, #e53e3e)', color: '#fff', borderColor: 'transparent' }
      }
    >
      {inRange ? 'in range' : 'out of range'}
    </span>
  );
}

export function InstrumentsScreen() {
  const [instruments, setInstruments] = useState<InstrumentDTO[] | null>(null);
  const pushToast = useApp((s) => s.pushToast);
  const readings = useApp((s) => s.instrumentReadings);
  const upsertReading = useApp((s) => s.upsertReading);

  const load = useCallback(() => {
    api
      .instruments()
      .then(async (rows: InstrumentDTO[]) => {
        setInstruments(rows);
        // Fetch the latest reading for each instrument; fire-and-forget per id.
        rows.forEach((ins) => {
          api
            .instrumentRead(ins.id)
            .then(upsertReading)
            .catch(() => {});
        });
      })
      .catch((e) => {
        setInstruments([]);
        pushToast('bad', `Couldn't load instruments: ${(e as Error).message}`);
      });
  }, [pushToast, upsertReading]);

  useEffect(() => {
    load();
  }, [load]);

  // Live readings via WS.
  useEffect(() => {
    socket.subscribe(['instrument_reading']);
    return () => socket.unsubscribe(['instrument_reading']);
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

      {instruments === null ? (
        <Loading />
      ) : instruments.length === 0 ? (
        <EmptyState
          icon={<Icon name="instrument" size={34} />}
          title="No instruments"
          hint="Lab instruments announce themselves when connected to the hub."
        />
      ) : (
        instruments.map((ins) => {
          const r = readings[ins.id];
          return (
            <div key={ins.id} className="row" style={{ alignItems: 'flex-start' }}>
              <span style={{ color: 'var(--accent)', flex: '0 0 auto', marginTop: 2 }}>
                <Icon name="instrument" size={20} />
              </span>
              <div style={{ flex: 1 }}>
                <div
                  style={{
                    fontWeight: 600,
                    display: 'flex',
                    gap: 8,
                    alignItems: 'center',
                    flexWrap: 'wrap',
                  }}
                >
                  {ins.name}
                  <InRangePill inRange={r?.in_range} />
                </div>
                <div className="faint">
                  {ins.device_class} · ch {ins.channel}
                  {ins.expected_min != null && ins.expected_max != null
                    ? ` · expected ${ins.expected_min}–${ins.expected_max} ${ins.unit}`
                    : ''}
                </div>
              </div>
              <div
                style={{
                  fontWeight: 700,
                  fontSize: 16,
                  color: r
                    ? r.in_range
                      ? 'var(--good)'
                      : 'var(--danger, #e53e3e)'
                    : 'var(--text-faint)',
                  textAlign: 'right',
                  minWidth: 80,
                }}
              >
                {formatValue(r?.value, ins.unit)}
              </div>
            </div>
          );
        })
      )}
    </div>
  );
}
