import { useComputed, useSignal } from '@preact/signals';
import { poolState } from '../stores/state';
import { poolWs } from '../lib/ws';
import { randomId } from '../lib/ids';
import { SectionTabs, TABS_TUNE } from '../components/SectionTabs';

function send(payload: object) {
  poolWs.send({ type: 'cmd', id: randomId(), payload: JSON.stringify(payload) });
}

export function Tanks() {
  const s = useComputed(() => poolState.value);
  const acidVol = useSignal('20');
  const chlVol  = useSignal('20');
  const phFlow  = useSignal('1.5');
  const chlFlow = useSignal('1.5');

  if (!s.value) return <div class="glass p-6">Loading…</div>;

  return (
    <div class="space-y-4 max-w-2xl">
      <SectionTabs current="/tune/tanks" tabs={TABS_TUNE} />
      <h1 class="text-xl font-bold">Tanks</h1>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">Acid (pH)</h2>
        <div class="flex items-baseline gap-2">
          <span class="text-3xl font-bold val-num">{s.value.tanks.acid_fill_pct}</span>
          <span class="text-sm opacity-50">% full</span>
        </div>
        <div class="grid grid-cols-2 gap-3">
          <label class="block">
            <div class="label-caps mb-1">Volume (L)</div>
            <input type="number" value={acidVol.value} onInput={e => (acidVol.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
          <label class="block">
            <div class="label-caps mb-1">Pump flow (L/h)</div>
            <input type="number" step="0.1" value={phFlow.value} onInput={e => (phFlow.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
        </div>
        <div class="flex gap-2">
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                  onClick={() => send({ pHTank: [Number(acidVol.value), 100] })}>Mark as refilled (100%)</button>
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary/15 border border-aqua-primary/35 text-cyan-200"
                  onClick={() => send({ pHPumpFR: Number(phFlow.value) })}>Save flow</button>
        </div>
      </div>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">Chlorine</h2>
        <div class="flex items-baseline gap-2">
          <span class="text-3xl font-bold val-num">{s.value.tanks.chl_fill_pct}</span>
          <span class="text-sm opacity-50">% full</span>
        </div>
        <div class="grid grid-cols-2 gap-3">
          <label class="block">
            <div class="label-caps mb-1">Volume (L)</div>
            <input type="number" value={chlVol.value} onInput={e => (chlVol.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
          <label class="block">
            <div class="label-caps mb-1">Pump flow (L/h)</div>
            <input type="number" step="0.1" value={chlFlow.value} onInput={e => (chlFlow.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
        </div>
        <div class="flex gap-2">
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                  onClick={() => send({ ChlTank: [Number(chlVol.value), 100] })}>Mark as refilled (100%)</button>
          <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary/15 border border-aqua-primary/35 text-cyan-200"
                  onClick={() => send({ ChlPumpFR: Number(chlFlow.value) })}>Save flow</button>
        </div>
      </div>
    </div>
  );
}
