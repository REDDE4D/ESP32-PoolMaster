import type { PresetSlot } from '../stores/state';

interface PresetCardProps {
  preset: PresetSlot;
  active: boolean;
  onActivate: () => void;
  onEdit: () => void;
}

function fmt(min: number): string {
  const h = Math.floor(min / 60);
  const m = min % 60;
  return `${h < 10 ? '0' : ''}${h}:${m < 10 ? '0' : ''}${m}`;
}

export function PresetCard({ preset, active, onActivate, onEdit }: PresetCardProps) {
  const enabled = preset.windows.filter(w => w.enabled);
  const summary = enabled.length === 0
    ? 'no windows'
    : enabled.length === 1
      ? `${fmt(enabled[0]!.start)} – ${fmt(enabled[0]!.end)}`
      : `${enabled.length} windows`;

  return (
    <div
      class={`rounded-lg border p-3 cursor-pointer flex flex-col min-h-[140px] transition-colors ${
        active
          ? 'border-aqua-primary bg-aqua-primary/8 shadow-[0_0_16px_-4px_rgba(34,211,238,0.5)]'
          : 'border-aqua-border bg-white/5 hover:bg-white/8'
      }`}
      onClick={onActivate}
    >
      <button
        class="absolute top-2 right-3 text-aqua-label/70 hover:text-aqua-label text-sm"
        onClick={e => { e.stopPropagation(); onEdit(); }}
        aria-label={`Edit ${preset.name}`}
      >
        ✎
      </button>

      <div class="flex items-center justify-between mb-2 pr-5">
        <span class="font-semibold text-sm">{preset.name}</span>
        <span class="text-[0.6rem] uppercase tracking-wider px-1.5 py-0.5 rounded-full bg-aqua-label/15 text-aqua-label">
          {preset.type === 'auto_temp' ? 'AUTO' : 'MANUAL'}
        </span>
      </div>

      <div class="relative h-5 bg-black/25 rounded my-auto mb-2 overflow-hidden">
        <div class="absolute inset-y-0 w-px bg-white/10" style={{ left: '25%' }} />
        <div class="absolute inset-y-0 w-px bg-white/10" style={{ left: '50%' }} />
        <div class="absolute inset-y-0 w-px bg-white/10" style={{ left: '75%' }} />
        {preset.windows.filter(w => w.enabled).map((w, i) => (
          <div
            key={i}
            class={`absolute inset-y-0.5 rounded-sm ${active ? 'bg-aqua-primary' : 'bg-aqua-primary/55'}`}
            style={{
              left: `${(w.start / 1440) * 100}%`,
              width: `${((w.end - w.start) / 1440) * 100}%`,
            }}
          />
        ))}
      </div>

      <div class="flex items-center justify-between text-[0.7rem]">
        <span class="font-mono opacity-60">{summary}</span>
        <span class={active ? 'text-aqua-primary font-semibold' : 'opacity-60'}>
          {active ? '● ACTIVE' : 'Activate'}
        </span>
      </div>
    </div>
  );
}
