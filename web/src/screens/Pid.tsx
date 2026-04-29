import { useComputed, useSignal } from '@preact/signals';
import { poolState } from '../stores/state';
import { Toggle } from '../components/Toggle';
import { poolWs } from '../lib/ws';
import { randomId } from '../lib/ids';
import { SectionTabs, TABS_TUNE } from '../components/SectionTabs';

interface Form { kp: string; ki: string; kd: string; window_min: string }

function send(payload: object) {
  poolWs.send({ type: 'cmd', id: randomId(), payload: JSON.stringify(payload) });
}

export function Pid() {
  const s = useComputed(() => poolState.value);
  const phForm  = useSignal<Form>({ kp: '', ki: '', kd: '', window_min: '' });
  const orpForm = useSignal<Form>({ kp: '', ki: '', kd: '', window_min: '' });

  if (!s.value) return <div class="glass p-6">Loading…</div>;

  const Row = ({ name, v, onI }: { name: keyof Form; v: string; onI: (s: string) => void }) => (
    <label class="block">
      <div class="label-caps mb-1">{name}</div>
      <input type="number" step={name === 'kp' ? 1000 : 0.001} value={v}
             onInput={e => onI((e.target as HTMLInputElement).value)}
             class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
    </label>
  );

  const submitPh = () => {
    const params = [Number(phForm.value.kp) || 0, Number(phForm.value.ki) || 0, Number(phForm.value.kd) || 0];
    send({ PhPIDParams: params });
    if (phForm.value.window_min) send({ PhPIDWSize: Number(phForm.value.window_min) * 60000 });
  };
  const submitOrp = () => {
    const params = [Number(orpForm.value.kp) || 0, Number(orpForm.value.ki) || 0, Number(orpForm.value.kd) || 0];
    send({ OrpPIDParams: params });
    if (orpForm.value.window_min) send({ OrpPIDWSize: Number(orpForm.value.window_min) * 60000 });
  };

  return (
    <div class="space-y-4 max-w-3xl">
      <SectionTabs current="/tune/pid" tabs={TABS_TUNE} />
      <h1 class="text-xl font-bold">PID tuning</h1>

      <div class="glass p-5 space-y-3">
        <div class="flex items-center justify-between">
          <h2 class="font-semibold">pH regulator</h2>
          <Toggle on={s.value.modes.ph_pid} onChange={on => send({ PhPID: on ? 1 : 0 })} label="Enable" />
        </div>
        <div class="grid grid-cols-2 md:grid-cols-4 gap-3">
          <Row name="kp" v={phForm.value.kp} onI={v => (phForm.value = { ...phForm.value, kp: v })} />
          <Row name="ki" v={phForm.value.ki} onI={v => (phForm.value = { ...phForm.value, ki: v })} />
          <Row name="kd" v={phForm.value.kd} onI={v => (phForm.value = { ...phForm.value, kd: v })} />
          <Row name="window_min" v={phForm.value.window_min} onI={v => (phForm.value = { ...phForm.value, window_min: v })} />
        </div>
        <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold" onClick={submitPh}>Save pH PID</button>
      </div>

      <div class="glass p-5 space-y-3">
        <div class="flex items-center justify-between">
          <h2 class="font-semibold">ORP regulator</h2>
          <Toggle on={s.value.modes.orp_pid} onChange={on => send({ OrpPID: on ? 1 : 0 })} label="Enable" />
        </div>
        <div class="grid grid-cols-2 md:grid-cols-4 gap-3">
          <Row name="kp" v={orpForm.value.kp} onI={v => (orpForm.value = { ...orpForm.value, kp: v })} />
          <Row name="ki" v={orpForm.value.ki} onI={v => (orpForm.value = { ...orpForm.value, ki: v })} />
          <Row name="kd" v={orpForm.value.kd} onI={v => (orpForm.value = { ...orpForm.value, kd: v })} />
          <Row name="window_min" v={orpForm.value.window_min} onI={v => (orpForm.value = { ...orpForm.value, window_min: v })} />
        </div>
        <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold" onClick={submitOrp}>Save ORP PID</button>
      </div>

      <p class="text-xs opacity-60">Leave a field blank to keep the existing value. Window length is in minutes.</p>
    </div>
  );
}
