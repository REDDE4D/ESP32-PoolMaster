import { signal } from '@preact/signals';

export type SeriesName = 'ph' | 'orp' | 'water_temp' | 'air_temp' | 'pressure';

export interface Series {
  name: SeriesName;
  t0_ms: number;
  step_s: number;
  values: number[];
}

export const history = signal<Record<SeriesName, Series | null>>({
  ph: null, orp: null, water_temp: null, air_temp: null, pressure: null,
});

export function setSeries(s: Series) {
  history.value = { ...history.value, [s.name]: s };
}

export function appendToSeries(name: SeriesName, values: number[]) {
  const cur = history.value[name];
  if (!cur) return;
  const MAX = 120;
  const next = cur.values.concat(values);
  if (next.length > MAX) next.splice(0, next.length - MAX);
  history.value = { ...history.value, [name]: { ...cur, values: next } };
}
