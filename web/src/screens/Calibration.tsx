import { useSignal, useComputed } from '@preact/signals';
import { poolState } from '../stores/state';
import { Modal } from '../components/Modal';
import { Badge } from '../components/Badge';
import { poolWs } from '../lib/ws';
import { fmtNum } from '../lib/format';
import { randomId } from '../lib/ids';
import { SectionTabs, TABS_TUNE } from '../components/SectionTabs';

type Probe = 'ph' | 'orp';

interface Reading { raw: number; buffer: number }

export function Calibration() {
  const probe      = useSignal<Probe>('ph');
  const running    = useSignal(false);
  const readings   = useSignal<Reading[]>([]);
  const currentBuffer = useSignal('');

  const state = useComputed(() => poolState.value);

  const liveRaw = useComputed(() => {
    if (!state.value) return NaN;
    return probe.value === 'ph' ? state.value.measurements.ph : state.value.measurements.orp;
  });

  const startWizard = (p: Probe) => {
    probe.value = p;
    readings.value = [];
    currentBuffer.value = '';
    running.value = true;
  };

  const addReading = () => {
    const bufVal = Number(currentBuffer.value);
    if (!Number.isFinite(bufVal)) return;
    readings.value = [...readings.value, { raw: Number(liveRaw.value) || 0, buffer: bufVal }];
    currentBuffer.value = '';
  };

  const finish = () => {
    const key = probe.value === 'ph' ? 'PhCalib' : 'OrpCalib';
    const flat: number[] = [];
    readings.value.forEach(r => flat.push(r.raw, r.buffer));
    poolWs.send({ type: 'cmd', id: randomId(), payload: JSON.stringify({ [key]: flat }) });
    running.value = false;
  };

  return (
    <div class="space-y-4 max-w-2xl">
      <SectionTabs current="/tune" tabs={TABS_TUNE} />
      <h1 class="text-xl font-bold">Calibration</h1>

      <div class="glass p-5">
        <h2 class="font-semibold mb-1">pH probe</h2>
        <p class="text-sm opacity-70 mb-3">Dip the probe into a buffer solution (pH 4.01 or 7.01), wait for the reading to stabilise, then capture it.</p>
        <button class="text-sm px-4 py-2 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                onClick={() => startWizard('ph')}>Start pH calibration</button>
      </div>

      <div class="glass p-5">
        <h2 class="font-semibold mb-1">ORP probe</h2>
        <p class="text-sm opacity-70 mb-3">Dip the probe into an ORP reference (e.g. 230 mV or 475 mV), wait for the reading to stabilise, then capture it.</p>
        <button class="text-sm px-4 py-2 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                onClick={() => startWizard('orp')}>Start ORP calibration</button>
      </div>

      <Modal
        open={running.value}
        title={`${probe.value === 'ph' ? 'pH' : 'ORP'} calibration`}
        onClose={() => (running.value = false)}
        footer={
          <>
            <button class="text-sm px-4 py-1.5 rounded-md bg-white/5 border border-aqua-border"
                    onClick={() => (running.value = false)}>Cancel</button>
            <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold disabled:opacity-50"
                    disabled={readings.value.length < 1}
                    onClick={finish}>
              Save ({readings.value.length} point{readings.value.length === 1 ? '' : 's'})
            </button>
          </>
        }
      >
        <div class="glass p-3">
          <div class="label-caps">Live reading</div>
          <div class="text-3xl font-bold val-num mt-1">
            {fmtNum(Number(liveRaw.value), probe.value === 'ph' ? 2 : 0)}
            <span class="text-sm opacity-50 ml-1 font-normal">
              {probe.value === 'ph' ? 'pH' : 'mV'}
            </span>
          </div>
          <div class="text-xs opacity-60 mt-1">Wait until this value stabilises before capturing.</div>
        </div>

        <div class="flex gap-2 items-end">
          <label class="flex-1">
            <div class="label-caps mb-1">Buffer value</div>
            <input
              type="number"
              step={probe.value === 'ph' ? 0.01 : 1}
              placeholder={probe.value === 'ph' ? '7.01' : '475'}
              value={currentBuffer.value}
              onInput={e => (currentBuffer.value = (e.target as HTMLInputElement).value)}
              class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm"
            />
          </label>
          <button class="px-3 py-1.5 rounded-md bg-aqua-primary/15 border border-aqua-primary/35 text-cyan-200 text-sm font-semibold"
                  onClick={addReading}
                  disabled={!currentBuffer.value}>
            Capture
          </button>
        </div>

        {readings.value.length > 0 && (
          <div class="glass p-3">
            <div class="label-caps mb-1">Captured points</div>
            <ul class="text-sm space-y-1">
              {readings.value.map((r, i) => (
                <li key={i} class="val-num">
                  {fmtNum(r.raw, probe.value === 'ph' ? 2 : 0)} → {fmtNum(r.buffer, 2)}
                </li>
              ))}
            </ul>
          </div>
        )}

        <Badge variant="info">Two points produce a linear calibration; more points improve accuracy (max 3).</Badge>
      </Modal>
    </div>
  );
}
