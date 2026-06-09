//
// log.ts — tiny structured logger.
//
// No dependency. Emits one JSON object per line (easy to ingest in Fly/Render
// log drains) with a level, timestamp, message, and arbitrary structured
// fields. Level is filtered by the LOG_LEVEL env var (default "info").
//
// PRIVACY: this logger is only ever called with routing/operational metadata —
// home_id, method, path, status, request ids, counts. It MUST NOT be handed
// request/response bodies, Authorization headers, or any other application
// data. The relay is a blind pipe; its logs reflect that.
//

type Level = 'debug' | 'info' | 'warn' | 'error';

const ORDER: Record<Level, number> = { debug: 10, info: 20, warn: 30, error: 40 };

function thresholdFromEnv(): number {
  const raw = (process.env.LOG_LEVEL ?? 'info').toLowerCase() as Level;
  return ORDER[raw] ?? ORDER.info;
}

const threshold = thresholdFromEnv();

/** A flat bag of structured fields attached to a log line. */
export type Fields = Record<string, unknown>;

function emit(level: Level, msg: string, fields?: Fields): void {
  if (ORDER[level] < threshold) return;
  const line: Fields = {
    t: new Date().toISOString(),
    level,
    msg,
    ...fields,
  };
  // One line per record. stderr for warn/error so they can be split out.
  const sink = level === 'warn' || level === 'error' ? process.stderr : process.stdout;
  sink.write(JSON.stringify(line) + '\n');
}

export const log = {
  debug: (msg: string, fields?: Fields) => emit('debug', msg, fields),
  info: (msg: string, fields?: Fields) => emit('info', msg, fields),
  warn: (msg: string, fields?: Fields) => emit('warn', msg, fields),
  error: (msg: string, fields?: Fields) => emit('error', msg, fields),
};
