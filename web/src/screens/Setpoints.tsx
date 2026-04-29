import { useComputed } from '@preact/signals';
import { poolState } from '../stores/state';
import { Slider } from '../components/Slider';
import { poolWs } from '../lib/ws';
import { randomId } from '../lib/ids';
import { SectionTabs, TABS_CONTROL } from '../components/SectionTabs';

export function Setpoints() {
  const s = useComputed(() => poolState.value);
  if (!s.value) return <div class="glass p-6">Loading…</div>;
  const sp = s.value.setpoints;

  const send = (key: string, val: number) => {
    const payload = JSON.stringify({ [key]: val });
    poolWs.send({ type: 'cmd', id: randomId(), payload });
  };

  return (
    <div class="space-y-4 max-w-lg">
      <SectionTabs current="/control/setpoints" tabs={TABS_CONTROL} />
      <h1 class="text-xl font-bold">Setpoints</h1>

      <div class="glass p-5">
        <Slider label="pH target" value={sp.ph} min={6.5} max={8.0} step={0.1} unit="pH"
                onCommit={v => send('PhSetPoint', v)} />
      </div>
      <div class="glass p-5">
        <Slider label="ORP target" value={sp.orp} min={400} max={900} step={10} unit="mV"
                onCommit={v => send('OrpSetPoint', v)} />
      </div>
      <div class="glass p-5">
        <Slider label="Water temperature" value={sp.water_temp} min={15} max={35} step={0.5} unit="°C"
                onCommit={v => send('WSetPoint', v)} />
      </div>

      <p class="text-xs opacity-60">Values are saved to NVS on change; the device echoes back the new setpoint via WebSocket.</p>
    </div>
  );
}
