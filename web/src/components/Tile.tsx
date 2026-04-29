import type { ComponentChildren } from 'preact';

interface TileProps {
  label: string;
  value: ComponentChildren;
  unit?: string;
  sub?: ComponentChildren;
  class?: string;
}

export function Tile({ label, value, unit, sub, class: className = '' }: TileProps) {
  return (
    <div class={`glass p-3 ${className}`}>
      <div class="label-caps">{label}</div>
      <div class="text-2xl font-bold val-num leading-tight mt-1">
        {value}{unit && <span class="text-sm opacity-50 font-normal ml-1">{unit}</span>}
      </div>
      {sub && <div class="text-xs opacity-60 mt-1">{sub}</div>}
    </div>
  );
}
