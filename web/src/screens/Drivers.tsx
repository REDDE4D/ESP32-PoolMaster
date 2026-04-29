import { useSignal } from '@preact/signals';
import { useEffect } from 'preact/hooks';
import { apiGet, apiPostForm } from '../lib/api';
import { SectionTabs, TABS_SETTINGS } from '../components/SectionTabs';

interface DriverCfg {
  slot: string;
  kind: 'gpio' | 'mqtt';
  pin: number;
  active_level: 'low' | 'high';
  cmd_topic: string;
  payload_on: string;
  payload_off: string;
  state_topic: string;
  state_on: string;
  state_off: string;
}

const SLOT_LABEL: Record<string, string> = {
  filt:  'Filtration pump',
  ph:    'pH dosing pump',
  chl:   'Chlorine dosing pump',
  robot: 'Robot pump',
  r0:    'Relay R0 — Projecteur',
  r1:    'Relay R1 — Spare',
};

function toFormPayload(c: DriverCfg): Record<string, string> {
  return {
    slot:         c.slot,
    kind:         c.kind,
    pin:          String(c.pin),
    active_level: c.active_level,
    cmd_topic:    c.cmd_topic,
    payload_on:   c.payload_on,
    payload_off:  c.payload_off,
    state_topic:  c.state_topic,
    state_on:     c.state_on,
    state_off:    c.state_off,
  };
}

export function Drivers() {
  const configs = useSignal<DriverCfg[] | null>(null);
  const saving  = useSignal<string | null>(null);

  useEffect(() => {
    let alive = true;
    (async () => {
      const res = await apiGet<DriverCfg[]>('/api/drivers');
      if (alive && res.ok && res.data) configs.value = res.data;
    })();
    return () => { alive = false; };
  }, []);

  if (!configs.value) {
    return (
      <div class="space-y-4 max-w-3xl">
        <SectionTabs current="/settings/drivers" tabs={TABS_SETTINGS} />
        <div class="glass p-6">Loading drivers…</div>
      </div>
    );
  }

  const update = (slot: string, patch: Partial<DriverCfg>) => {
    configs.value = (configs.value ?? []).map(c => c.slot === slot ? { ...c, ...patch } : c);
  };

  const save = async (cfg: DriverCfg) => {
    if (cfg.kind === 'mqtt' && !cfg.cmd_topic) {
      alert('MQTT kind requires a command topic');
      return;
    }
    if (!cfg.payload_on || !cfg.payload_off) {
      alert('Payloads may not be empty');
      return;
    }
    saving.value = cfg.slot;
    const res = await apiPostForm('/api/drivers', toFormPayload(cfg));
    saving.value = null;
    alert(res.ok ? 'Saved — device rebooting. Reconnect in ~15s.' : `Failed: ${res.error}`);
  };

  return (
    <div class="space-y-4 max-w-3xl">
      <SectionTabs current="/settings/drivers" tabs={TABS_SETTINGS} />
      <h1 class="text-xl font-bold">Output drivers</h1>
      <p class="text-xs opacity-70">
        Each physical output can be driven by a local GPIO pin or via MQTT to an external relay
        (Shelly, Tasmota, etc). Defaults match the original wiring — change only if your hardware
        setup is different.
      </p>

      {configs.value.map(cfg => (
        <div key={cfg.slot} class="glass p-5 space-y-3">
          <div class="flex items-center justify-between">
            <h2 class="font-semibold">
              {SLOT_LABEL[cfg.slot] ?? cfg.slot}
              <span class={`ml-2 inline-block text-[0.7rem] px-2 py-0.5 rounded-full border ${
                cfg.kind === 'gpio'
                  ? 'bg-aqua-ok/15 text-emerald-300 border-emerald-500/30'
                  : 'bg-aqua-info/15 text-cyan-300 border-cyan-500/30'
              }`}>
                {cfg.kind}
              </span>
            </h2>
          </div>

          <div class="flex gap-2">
            {(['gpio', 'mqtt'] as const).map(k => (
              <button key={k}
                class={`text-xs px-3 py-1.5 rounded-md border ${
                  cfg.kind === k
                    ? 'bg-aqua-primary/25 border-aqua-primary/50 text-cyan-100'
                    : 'bg-white/5 border-aqua-border text-aqua-text/70'
                }`}
                onClick={() => update(cfg.slot, { kind: k })}>
                {k === 'gpio' ? 'GPIO' : 'MQTT'}
              </button>
            ))}
          </div>

          {cfg.kind === 'gpio' ? (
            <div class="grid grid-cols-2 gap-3">
              <label class="block">
                <div class="label-caps mb-1">GPIO pin (0–39)</div>
                <input type="number" min={0} max={39} value={cfg.pin}
                       onInput={e => update(cfg.slot, { pin: Number((e.target as HTMLInputElement).value) })}
                       class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
              </label>
              <label class="block">
                <div class="label-caps mb-1">Active level</div>
                <div class="flex gap-2">
                  {(['low', 'high'] as const).map(al => (
                    <button key={al}
                      class={`text-xs px-3 py-1.5 rounded-md border ${
                        cfg.active_level === al
                          ? 'bg-aqua-primary/25 border-aqua-primary/50 text-cyan-100'
                          : 'bg-white/5 border-aqua-border text-aqua-text/70'
                      }`}
                      onClick={() => update(cfg.slot, { active_level: al })}>
                      {al === 'low' ? 'Active low (default)' : 'Active high'}
                    </button>
                  ))}
                </div>
              </label>
            </div>
          ) : (
            <div class="grid grid-cols-2 gap-3">
              <label class="block col-span-2">
                <div class="label-caps mb-1">Command topic (required)</div>
                <input type="text" value={cfg.cmd_topic}
                       placeholder="shellies/garden-filter/relay/0/command"
                       onInput={e => update(cfg.slot, { cmd_topic: (e.target as HTMLInputElement).value })}
                       class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
              </label>
              <label class="block">
                <div class="label-caps mb-1">Payload for ON</div>
                <input type="text" value={cfg.payload_on} placeholder="on"
                       onInput={e => update(cfg.slot, { payload_on: (e.target as HTMLInputElement).value })}
                       class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
              </label>
              <label class="block">
                <div class="label-caps mb-1">Payload for OFF</div>
                <input type="text" value={cfg.payload_off} placeholder="off"
                       onInput={e => update(cfg.slot, { payload_off: (e.target as HTMLInputElement).value })}
                       class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
              </label>
              <label class="block col-span-2">
                <div class="label-caps mb-1">
                  State topic (optional — empty = fire-and-forget, Pump will report "not running")
                </div>
                <input type="text" value={cfg.state_topic}
                       placeholder="shellies/garden-filter/relay/0"
                       onInput={e => update(cfg.slot, { state_topic: (e.target as HTMLInputElement).value })}
                       class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
              </label>
              {cfg.state_topic && (
                <>
                  <label class="block">
                    <div class="label-caps mb-1">State-on payload</div>
                    <input type="text" value={cfg.state_on} placeholder="on"
                           onInput={e => update(cfg.slot, { state_on: (e.target as HTMLInputElement).value })}
                           class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
                  </label>
                  <label class="block">
                    <div class="label-caps mb-1">State-off payload</div>
                    <input type="text" value={cfg.state_off} placeholder="off"
                           onInput={e => update(cfg.slot, { state_off: (e.target as HTMLInputElement).value })}
                           class="w-full bg-slate-900/50 border border-aqua-border rounded-md px-3 py-1.5 text-sm" />
                  </label>
                </>
              )}
            </div>
          )}

          <button
            class="text-sm px-4 py-1.5 rounded-md bg-aqua-primary text-slate-900 font-semibold disabled:opacity-50"
            disabled={saving.value === cfg.slot}
            onClick={() => save(cfg)}>
            {saving.value === cfg.slot ? 'Saving…' : 'Save + reboot'}
          </button>
        </div>
      ))}
    </div>
  );
}
