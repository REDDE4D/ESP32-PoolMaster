import { useComputed } from '@preact/signals';
import { poolState } from '../stores/state';

const LABELS: Record<string, string> = {
  pressure: 'Pressure alarm',
  ph_pump_overtime: 'pH pump overtime',
  chl_pump_overtime: 'Chlorine pump overtime',
  acid_tank_low: 'Acid tank low',
  chl_tank_low: 'Chlorine tank low',
};

export function AlarmBanner() {
  const active = useComputed(() => {
    const s = poolState.value;
    if (!s) return [] as string[];
    return Object.entries(s.alarms).filter(([, v]) => v).map(([k]) => k);
  });

  if (active.value.length === 0) return null;

  return (
    <div class="bg-aqua-alarm/15 border border-aqua-alarm/40 text-rose-200 rounded-xl p-3 text-sm flex items-start gap-2">
      <span class="text-aqua-alarm text-lg leading-none">⚠</span>
      <div>
        <div class="font-semibold">
          {active.value.length === 1 ? '1 alarm' : `${active.value.length} alarms`} active
        </div>
        <div class="text-xs opacity-80">
          {active.value.map(id => LABELS[id] ?? id).join(' · ')}
        </div>
      </div>
    </div>
  );
}
