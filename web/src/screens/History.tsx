import { useComputed } from '@preact/signals';
import { history } from '../stores/history';
import { Chart } from '../components/Chart';
import { SectionTabs, TABS_INSIGHTS } from '../components/SectionTabs';

const SERIES: Array<{ name: keyof typeof history.value; label: string }> = [
  { name: 'ph',         label: 'pH' },
  { name: 'orp',        label: 'ORP (mV)' },
  { name: 'water_temp', label: 'Water temperature (°C)' },
  { name: 'air_temp',   label: 'Air temperature (°C)' },
  { name: 'pressure',   label: 'Pressure (bar)' },
];

export function History() {
  const h = useComputed(() => history.value);

  return (
    <div class="space-y-4 max-w-4xl">
      <SectionTabs current="/insights" tabs={TABS_INSIGHTS} />
      <h1 class="text-xl font-bold">History (last 60 min)</h1>
      {SERIES.map(s => {
        const series = h.value[s.name];
        if (!series) {
          return <div key={s.name} class="glass p-6 opacity-50">{s.label} — no data yet…</div>;
        }
        return (
          <div key={s.name} class="space-y-1">
            <div class="label-caps px-1">{s.label}</div>
            <Chart values={series.values} t0_ms={series.t0_ms} step_s={series.step_s} label={s.label} />
          </div>
        );
      })}
    </div>
  );
}
