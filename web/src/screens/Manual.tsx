import { useComputed } from '@preact/signals';
import { poolState } from '../stores/state';
import { Toggle } from '../components/Toggle';
import { Badge } from '../components/Badge';
import { poolWs } from '../lib/ws';
import { useAuthedAction } from '../components/AuthBoundary';
import { apiSendCommand } from '../lib/api';
import { randomId } from '../lib/ids';
import { SectionTabs, TABS_CONTROL } from '../components/SectionTabs';

function cmd(payload: string) {
  // Prefer WebSocket when open; REST fallback for auth-triggering POSTs.
  if (!poolWs.send({ type: 'cmd', id: randomId(), payload })) {
    return apiSendCommand(payload);
  }
  return Promise.resolve({ ok: true } as const);
}

interface Row {
  key: string;
  label: string;
  getState: (s: NonNullable<typeof poolState.value>) => boolean;
  on:  string;
  off: string;
}

// Map each toggle to its legacy JSON command.
const ROWS: Array<{ label: string; getOn: (s: NonNullable<typeof poolState.value>) => boolean; on: string; off: string }> = [
  { label: 'Auto mode',       getOn: s => s.modes.auto,       on: '{"Mode":1}',     off: '{"Mode":0}' },
  { label: 'Winter mode',     getOn: s => s.modes.winter,     on: '{"Winter":1}',   off: '{"Winter":0}' },
  { label: 'pH PID',          getOn: s => s.modes.ph_pid,     on: '{"PhPID":1}',    off: '{"PhPID":0}' },
  { label: 'ORP PID',         getOn: s => s.modes.orp_pid,    on: '{"OrpPID":1}',   off: '{"OrpPID":0}' },
  { label: 'Filtration pump', getOn: s => s.pumps.filtration, on: '{"FiltPump":1}', off: '{"FiltPump":0}' },
  { label: 'pH pump',         getOn: s => s.pumps.ph,         on: '{"PhPump":1}',   off: '{"PhPump":0}' },
  { label: 'Chlorine pump',   getOn: s => s.pumps.chl,        on: '{"ChlPump":1}',  off: '{"ChlPump":0}' },
  { label: 'Robot pump',      getOn: s => s.pumps.robot,      on: '{"RobotPump":1}',off: '{"RobotPump":0}' },
];

const RELAY_ROWS: Array<{ label: string; num: 0 | 1; getOn: (s: NonNullable<typeof poolState.value>) => boolean }> = [
  { label: 'Projecteur (R0)', num: 0, getOn: s => s.relays.r0 },
  { label: 'Spare (R1)',      num: 1, getOn: s => s.relays.r1 },
];

export function Manual() {
  const s = useComputed(() => poolState.value);

  if (!s.value) return <div class="glass p-6">Loading…</div>;

  const state = s.value;

  const toggle = (on: boolean, payload: string) => () => cmd(payload);

  return (
    <div class="space-y-4 max-w-xl">
      <SectionTabs current="/control" tabs={TABS_CONTROL} />
      <h1 class="text-xl font-bold">Manual control</h1>
      <div class="glass p-4 space-y-3">
        {ROWS.map(row => (
          <div key={row.label} class="flex items-center justify-between">
            <span class="text-sm">{row.label}</span>
            <Toggle
              on={row.getOn(state)}
              onChange={next => cmd(next ? row.on : row.off)}
            />
          </div>
        ))}
      </div>

      <div class="glass p-4 space-y-3">
        <div class="label-caps">Relays</div>
        {RELAY_ROWS.map(r => (
          <div key={r.num} class="flex items-center justify-between">
            <span class="text-sm">{r.label}</span>
            <Toggle
              on={r.getOn(state)}
              onChange={next => cmd(`{"Relay":[${r.num},${next ? 1 : 0}]}`)}
            />
          </div>
        ))}
      </div>

      <div class="glass p-4 flex gap-2 flex-wrap">
        <Badge variant="info">Click any toggle to flip — the state updates when the device acks via WebSocket.</Badge>
      </div>
    </div>
  );
}
