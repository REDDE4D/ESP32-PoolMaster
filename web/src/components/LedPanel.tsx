import { useComputed } from '@preact/signals';
import { poolState } from '../stores/state';
import { wsStatus } from '../lib/ws';

// Mirrors the PCF8574A bit layout driven by Loops.cpp::StatusLights — two
// display lines that the firmware multiplexes on the physical board. The UI
// shows both lines simultaneously so you can read every status without
// waiting for the hardware to flip layers.

type Slot =
  | { kind: 'led'; label: string; on: boolean; tone: 'ok' | 'warn' | 'alarm' | 'info' }
  | { kind: 'empty' };

interface Row {
  title: string;
  slots: Slot[];
}

const TONE: Record<'ok' | 'warn' | 'alarm' | 'info', { on: string; off: string; ring: string }> = {
  ok:    { on: 'bg-aqua-ok shadow-[0_0_10px_2px_rgba(52,211,153,0.55)]', off: 'bg-aqua-ok/15',   ring: 'ring-emerald-400/30' },
  warn:  { on: 'bg-aqua-warn shadow-[0_0_10px_2px_rgba(251,191,36,0.55)]', off: 'bg-aqua-warn/15', ring: 'ring-amber-400/30' },
  alarm: { on: 'bg-aqua-alarm shadow-[0_0_10px_2px_rgba(244,63,94,0.55)]', off: 'bg-aqua-alarm/15', ring: 'ring-rose-400/30' },
  info:  { on: 'bg-aqua-info shadow-[0_0_10px_2px_rgba(34,211,238,0.55)]', off: 'bg-aqua-info/15', ring: 'ring-cyan-400/30' },
};

function Led({ slot }: { slot: Slot }) {
  if (slot.kind === 'empty') {
    return (
      <div class="flex flex-col items-center gap-1 min-w-[3.5rem]">
        <div class="w-3 h-3 rounded-full bg-white/5 ring-1 ring-white/5" />
        <div class="text-[0.62rem] opacity-30 leading-tight text-center h-6">—</div>
      </div>
    );
  }
  const t = TONE[slot.tone];
  return (
    <div class="flex flex-col items-center gap-1 min-w-[3.5rem]">
      <div class={`w-3 h-3 rounded-full ring-1 ${t.ring} ${slot.on ? t.on : t.off}`} />
      <div class={`text-[0.62rem] leading-tight text-center h-6 ${slot.on ? 'opacity-90' : 'opacity-50'}`}>
        {slot.label}
      </div>
    </div>
  );
}

export function LedPanel() {
  const status = useComputed(() => wsStatus.value);
  const s = useComputed(() => poolState.value);
  if (!s.value) return null;
  const m = s.value.modes;
  const a = s.value.alarms;

  const wifiOn = status.value === 'connected';

  // Line 0: persistent connection bit + run-mode flags + pressure error
  const line0: Row = {
    title: 'Line 0',
    slots: [
      { kind: 'led', label: 'WiFi',       on: wifiOn,        tone: 'info' },
      { kind: 'led', label: 'line',       on: false,         tone: 'info' },
      { kind: 'led', label: 'Auto',       on: m.auto,        tone: 'ok' },
      { kind: 'led', label: 'Anti-freeze', on: m.antifreeze, tone: 'warn' },
      { kind: 'empty' },
      { kind: 'empty' },
      { kind: 'empty' },
      { kind: 'led', label: 'PSI alarm',  on: a.pressure,    tone: 'alarm' },
    ],
  };

  // Line 1: PID + tank/pump alarms; bit 1 stays lit while the line is showing
  const line1: Row = {
    title: 'Line 1',
    slots: [
      { kind: 'led', label: 'WiFi',       on: wifiOn,                  tone: 'info' },
      { kind: 'led', label: 'line',       on: true,                    tone: 'info' },
      { kind: 'led', label: 'pH PID',     on: m.ph_pid,                tone: 'ok' },
      { kind: 'led', label: 'ORP PID',    on: m.orp_pid,               tone: 'ok' },
      { kind: 'led', label: 'Acid low',   on: a.acid_tank_low,         tone: 'alarm' },
      { kind: 'led', label: 'Chl low',    on: a.chl_tank_low,          tone: 'alarm' },
      { kind: 'led', label: 'pH overrun', on: a.ph_pump_overtime,      tone: 'alarm' },
      { kind: 'led', label: 'Chl overrun', on: a.chl_pump_overtime,    tone: 'alarm' },
    ],
  };

  // Buzzer mirrors the firmware's (status & 0xF0) test — any of the alarm
  // bits across both lines drives the piezo on the physical board.
  const buzzerOn =
    a.pressure || a.acid_tank_low || a.chl_tank_low ||
    a.ph_pump_overtime || a.chl_pump_overtime;

  return (
    <div class="glass p-5">
      <div class="flex items-center justify-between mb-3">
        <h2 class="font-semibold">Status panel</h2>
        <span class="text-[0.7rem] opacity-60">PCF8574A · 8-bit, 2-line</span>
      </div>

      <div class="space-y-3">
        {[line0, line1].map(row => (
          <div key={row.title}>
            <div class="text-[0.62rem] uppercase tracking-wider opacity-40 mb-1">{row.title}</div>
            <div class="flex justify-between gap-1 px-1">
              {row.slots.map((slot, i) => <Led key={i} slot={slot} />)}
            </div>
          </div>
        ))}
      </div>

      <div class="mt-4 pt-3 border-t border-aqua-border flex items-center gap-2">
        <div class={`w-3 h-3 rounded-full ring-1 ${buzzerOn ? `${TONE.alarm.on} ${TONE.alarm.ring}` : `${TONE.alarm.off} ring-white/10`}`} />
        <div class="text-xs opacity-80">Buzzer</div>
        <div class="text-[0.7rem] opacity-50 ml-auto">{buzzerOn ? 'sounding' : 'silent'}</div>
      </div>
    </div>
  );
}
