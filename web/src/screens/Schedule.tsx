import { useComputed, useSignal } from '@preact/signals';
import { poolWs } from '../lib/ws';
import { randomId } from '../lib/ids';
import { poolState } from '../stores/state';
import type { PresetSlot } from '../stores/state';
import { PresetCard } from '../components/PresetCard';
import { PresetEditModal } from '../components/PresetEditModal';
import { SectionTabs, TABS_TUNE } from '../components/SectionTabs';

function sendCmd(payload: object) {
  poolWs.send({ type: 'cmd', id: randomId(), payload: JSON.stringify(payload) });
}

export function Schedule() {
  const editing = useSignal<PresetSlot | null>(null);
  const schedule = useComputed(() => poolState.value?.schedule ?? null);
  const delayPid = useSignal('30');

  if (!schedule.value) {
    return <div class="glass p-6">Loading schedule…</div>;
  }
  const { active_slot, presets } = schedule.value;

  const onActivate = (slot: number) => {
    if (slot === active_slot) return;
    sendCmd({ PresetActivate: { slot } });
  };
  const onEdit = (preset: PresetSlot) => {
    editing.value = preset;
  };
  const onSave = (next: PresetSlot) => {
    sendCmd({
      PresetSave: {
        slot: next.slot,
        name: next.name,
        presetType: next.type,
        windows: next.windows,
        auto: next.auto,
      },
    });
    editing.value = null;
  };
  const onDelete = (slot: number) => {
    sendCmd({ PresetDelete: { slot } });
    editing.value = null;
  };

  return (
    <div class="space-y-4 max-w-3xl">
      <SectionTabs current="/tune/schedule" tabs={TABS_TUNE} />
      <h1 class="text-xl font-bold">Schedule</h1>
      <p class="text-sm opacity-70">Choose an active preset. Edit any preset to adjust windows.</p>

      <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-5 gap-3">
        {presets.map(p => (
          <div key={p.slot} class="relative">
            <PresetCard
              preset={p}
              active={p.slot === active_slot}
              onActivate={() => onActivate(p.slot)}
              onEdit={() => onEdit(p)}
            />
          </div>
        ))}
      </div>

      <div class="glass p-5 space-y-3">
        <h2 class="font-semibold">Global</h2>
        <p class="text-xs opacity-70">DelayPIDs is shared across all presets — minutes to wait after filtration starts before PIDs begin regulating.</p>
        <div class="flex gap-2 items-end">
          <label class="flex-1">
            <div class="label-caps mb-1">DelayPID (minutes)</div>
            <input type="number" min={0} max={59} value={delayPid.value}
                   onInput={e => (delayPid.value = (e.target as HTMLInputElement).value)}
                   class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
          </label>
          <button class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold"
                  onClick={() => sendCmd({ DelayPID: Number(delayPid.value) })}>Save</button>
        </div>
      </div>

      {editing.value && (
        <PresetEditModal
          initial={editing.value}
          onSave={onSave}
          onDelete={() => onDelete(editing.value!.slot)}
          onClose={() => (editing.value = null)}
        />
      )}
    </div>
  );
}
