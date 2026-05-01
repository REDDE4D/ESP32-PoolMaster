import { useSignal } from '@preact/signals';
import type { PresetSlot, PresetType, PresetWindow } from '../stores/state';
import { TimePicker } from './TimePicker';

interface PresetEditModalProps {
  initial: PresetSlot;
  onSave: (next: PresetSlot) => void;
  onDelete: () => void;
  onClose: () => void;
}

const EMPTY_WINDOW: PresetWindow = { start: 0, end: 0, enabled: false };

export function PresetEditModal({ initial, onSave, onDelete, onClose }: PresetEditModalProps) {
  const name    = useSignal(initial.name);
  const type    = useSignal<PresetType>(initial.type);
  const windows = useSignal<PresetWindow[]>(
    [0, 1, 2, 3].map(i => initial.windows[i] ?? EMPTY_WINDOW)
  );
  const startMinHour = useSignal(initial.auto?.startMinHour ?? 8);
  const stopMaxHour  = useSignal(initial.auto?.stopMaxHour ?? 22);
  const centerHour   = useSignal(initial.auto?.centerHour ?? 15);

  const setWindow = (i: number, patch: Partial<PresetWindow>) => {
    const next = windows.value.slice();
    next[i] = { ...next[i]!, ...patch };
    windows.value = next;
  };

  const valid = (() => {
    if (!name.value.trim()) return false;
    if (name.value.length > 15) return false;
    if (type.value === 'auto_temp') {
      if (startMinHour.value >= stopMaxHour.value) return false;
      if (centerHour.value < startMinHour.value || centerHour.value > stopMaxHour.value) return false;
    } else {
      for (const w of windows.value) {
        if (!w.enabled) continue;
        if (w.start < 0 || w.start > 1439 || w.end < 0 || w.end > 1439) return false;
        if (w.start > w.end) return false;
      }
    }
    return true;
  })();

  const submit = () => {
    if (!valid) return;
    onSave({
      slot: initial.slot,
      name: name.value.trim(),
      type: type.value,
      windows: windows.value,
      auto: type.value === 'auto_temp'
        ? { startMinHour: startMinHour.value, stopMaxHour: stopMaxHour.value, centerHour: centerHour.value }
        : null,
    });
  };

  return (
    <div class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/50" onClick={onClose}>
      <div
        class="glass-elev max-w-md w-full p-5 space-y-4 rounded-lg border border-aqua-border-elev"
        onClick={e => e.stopPropagation()}
      >
        <h3 class="text-lg font-bold">Edit "{initial.name}"</h3>

        <label class="block">
          <div class="label-caps mb-1">Name</div>
          <input type="text" value={name.value} maxLength={15}
                 onInput={e => name.value = (e.target as HTMLInputElement).value}
                 class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
        </label>

        <div>
          <div class="label-caps mb-1">Type</div>
          <div class="flex gap-2">
            <button
              class={`flex-1 px-3 py-2 rounded-md text-sm border ${type.value === 'auto_temp'
                ? 'border-aqua-primary bg-aqua-primary/10 text-aqua-primary'
                : 'border-aqua-border bg-black/20'}`}
              onClick={() => type.value = 'auto_temp'}>Auto-temp</button>
            <button
              class={`flex-1 px-3 py-2 rounded-md text-sm border ${type.value === 'manual'
                ? 'border-aqua-primary bg-aqua-primary/10 text-aqua-primary'
                : 'border-aqua-border bg-black/20'}`}
              onClick={() => type.value = 'manual'}>Manual</button>
          </div>
        </div>

        {type.value === 'auto_temp' ? (
          <div class="bg-aqua-primary/8 border-l-2 border-aqua-primary p-3 rounded grid grid-cols-3 gap-2">
            <NumField label="Earliest start" value={startMinHour.value} min={0} max={23}
                      onChange={v => startMinHour.value = v} />
            <NumField label="Latest stop"    value={stopMaxHour.value}  min={1} max={23}
                      onChange={v => stopMaxHour.value = v} />
            <NumField label="Center hour"    value={centerHour.value}   min={0} max={23}
                      onChange={v => centerHour.value = v} />
          </div>
        ) : (
          <div class="bg-black/20 rounded p-3 space-y-2">
            {windows.value.map((w, i) => (
              <div key={i} class="flex items-center gap-3 text-sm">
                <input type="checkbox" checked={w.enabled}
                       onChange={e => setWindow(i, { enabled: (e.target as HTMLInputElement).checked })} />
                <TimePicker value={w.start} disabled={!w.enabled}
                            onChange={v => setWindow(i, { start: v })} />
                <span>—</span>
                <TimePicker value={w.end}   disabled={!w.enabled}
                            onChange={v => setWindow(i, { end: v })} />
              </div>
            ))}
          </div>
        )}

        <div class="flex justify-between items-center pt-2">
          <button class="text-xs px-3 py-1.5 rounded-md bg-aqua-alarm/15 border border-rose-500/40 text-rose-200"
                  onClick={onDelete}>Delete slot</button>
          <div class="flex gap-2">
            <button class="text-sm px-3 py-1.5 rounded-md bg-white/10 border border-aqua-border"
                    onClick={onClose}>Cancel</button>
            <button class="text-sm px-3 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold disabled:opacity-40"
                    disabled={!valid} onClick={submit}>Save</button>
          </div>
        </div>
      </div>
    </div>
  );
}

interface NumFieldProps {
  label: string;
  value: number;
  min: number;
  max: number;
  onChange: (v: number) => void;
}

function NumField({ label, value, min, max, onChange }: NumFieldProps) {
  return (
    <label class="block text-xs">
      <div class="opacity-70 mb-1">{label}</div>
      <input type="number" min={min} max={max} value={value}
             onInput={e => onChange(parseInt((e.target as HTMLInputElement).value, 10) || 0)}
             class="w-full bg-black/30 border border-aqua-border rounded px-2 py-1 text-sm font-mono text-center" />
    </label>
  );
}
