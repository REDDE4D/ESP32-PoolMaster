import { useSignal } from '@preact/signals';
import { poolWs } from '../lib/ws';
import { randomId } from '../lib/ids';
import { SectionTabs, TABS_TUNE } from '../components/SectionTabs';

function send(payload: object) {
  poolWs.send({ type: 'cmd', id: randomId(), payload: JSON.stringify(payload) });
}

export function Schedule() {
  const t0 = useSignal('8');
  const t1 = useSignal('20');
  const delayPid = useSignal('30');

  return (
    <div class="space-y-4 max-w-xl">
      <SectionTabs current="/tune/schedule" tabs={TABS_TUNE} />
      <h1 class="text-xl font-bold">Schedule</h1>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">Filtration</h2>
        <div class="grid grid-cols-2 gap-3">
          <label class="block">
            <div class="label-caps mb-1">Earliest start (hour)</div>
            <input type="number" min={0} max={23} value={t0.value}
                   onInput={e => (t0.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
          <label class="block">
            <div class="label-caps mb-1">Latest stop (hour)</div>
            <input type="number" min={0} max={23} value={t1.value}
                   onInput={e => (t1.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
        </div>
        <div class="flex gap-2">
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                  onClick={() => send({ FiltT0: Number(t0.value) })}>Save start</button>
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                  onClick={() => send({ FiltT1: Number(t1.value) })}>Save stop</button>
        </div>
      </div>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">PID delay after filter start</h2>
        <p class="text-xs opacity-70">Minutes to wait after filtration starts before PID loops begin regulating (lets readings stabilise).</p>
        <div class="flex gap-2 items-end">
          <label class="flex-1">
            <div class="label-caps mb-1">Delay (minutes)</div>
            <input type="number" min={0} max={59} value={delayPid.value}
                   onInput={e => (delayPid.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
          <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                  onClick={() => send({ DelayPID: Number(delayPid.value) })}>Save</button>
        </div>
      </div>
    </div>
  );
}
