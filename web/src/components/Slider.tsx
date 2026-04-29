import { useState } from 'preact/hooks';

interface SliderProps {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  unit?: string;
  onCommit: (next: number) => void;
}

export function Slider({ label, value, min, max, step, unit, onCommit }: SliderProps) {
  const [draft, setDraft] = useState(value);

  return (
    <div class="space-y-1">
      <div class="flex items-baseline justify-between">
        <span class="label-caps">{label}</span>
        <span class="val-num font-semibold text-aqua-text">
          {draft}{unit && <span class="text-sm opacity-50 font-normal ml-1">{unit}</span>}
        </span>
      </div>
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={draft}
        onInput={e => setDraft(Number((e.target as HTMLInputElement).value))}
        onChange={e => onCommit(Number((e.target as HTMLInputElement).value))}
        class="w-full accent-aqua-primary"
      />
      <div class="flex justify-between text-xs opacity-50 val-num">
        <span>{min}</span><span>{max}</span>
      </div>
    </div>
  );
}
