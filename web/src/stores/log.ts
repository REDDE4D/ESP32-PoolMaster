import { signal } from '@preact/signals';

export interface LogEntry {
  ts: number;
  level: 'dbg' | 'inf' | 'wrn' | 'err' | string;
  msg: string;
}

export const logEntries = signal<LogEntry[]>([]);

const MAX = 500;

export function appendLog(e: LogEntry) {
  const next = logEntries.value.concat(e);
  if (next.length > MAX) next.splice(0, next.length - MAX);
  logEntries.value = next;
}

export function replaceLogs(entries: LogEntry[]) {
  logEntries.value = entries.slice(-MAX);
}

export function clearLogs() {
  logEntries.value = [];
}
