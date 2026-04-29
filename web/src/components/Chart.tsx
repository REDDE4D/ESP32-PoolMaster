import { useEffect, useRef } from 'preact/hooks';
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';

interface ChartProps {
  values: number[];
  t0_ms: number;
  step_s: number;
  label: string;
  height?: number;
}

export function Chart({ values, t0_ms, step_s, label, height = 160 }: ChartProps) {
  const ref = useRef<HTMLDivElement>(null);
  const plotRef = useRef<uPlot | null>(null);

  useEffect(() => {
    if (!ref.current) return;
    const xs = values.map((_, i) => (t0_ms + i * step_s * 1000) / 1000);
    const ys = values.map(v => (Number.isFinite(v) ? v : null));

    if (plotRef.current) {
      plotRef.current.setData([xs, ys]);
      return;
    }

    plotRef.current = new uPlot(
      {
        width: ref.current.offsetWidth || 400,
        height,
        scales: { x: { time: true } },
        axes: [
          { stroke: '#7dd3fc', grid: { stroke: 'rgba(125,211,252,0.1)' } },
          { stroke: '#7dd3fc', grid: { stroke: 'rgba(125,211,252,0.1)' } },
        ],
        series: [
          {},
          { label, stroke: '#22d3ee', width: 2, fill: 'rgba(34,211,238,0.1)' },
        ],
      },
      [xs, ys],
      ref.current,
    );

    return () => { plotRef.current?.destroy(); plotRef.current = null; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [values, t0_ms, step_s]);

  return <div ref={ref} class="glass p-2" />;
}
